// FIXME: ensure end of interface is called 'result'
// FIXME: make sure comment function title matches actual
// FIXME: ensure support functions do not have TRACE's
// FIXME: consider calculating jack latency and adding
// FIXME: consider folding buffer index switcher into function of its own
// FIXME: make sure TRUE/FALSE is used with BOOL, and ASIOTrue/False used *Error
// FIXME: write note explaining 64-bit endian order in tunables.h
// FIXME: make sure support functions are in correct order
// FIXME: make sure support functions have forward declarations
// FIXME: make sure all memory allocations are freed() on exit
// FIXME: make sure all jack_ calls are reversed on exit
// FIXME: make sure all preprocessor symbols are prefixed with WINEASIO_
// FIXME: respect right margin
// FIXME: make sure order of instance variables matches initialization
// FIXME: make sure all numeric TRACE parameters are in () and strings in `'
// FIXME: rename all jack_ functions to wineasio_?
// FIXME: do away with the fucked 'const' alignment
// FIXME: comment all instance variables at their declaration
// FIXME: change ERR's to WARN's
// FIXME: make sure all functions, structures, forward declarations have a comment
// FIXME: add internal error for Windows ASIO Host program for bad state transition
// FIXME: make sure 'JACK' is all-caps in comments

/*
 * Copyright (C) 2006 Robert Reif
 * Portions copyright (C) 2007 Ralf Beck
 * Portions copyright (C) 2007 Johnny Petrantoni
 * Portions copyright (C) 2007 Stephane Letz
 * Portions copyright (C) 2008 William Steidtmann
 * Portions copyright (C) 2010 Peter L Jones
 * Portions copyright (C) 2010 Torben Hohn
 * Portions copyright (C) 2010 Nedko Arnaudov
 * Portions copyright (C) 2013 Joakim Hernberg
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>

#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>

#include <wine/debug.h>
// #include <wine/unicode.h>

#include <jack/jack.h>
#include <jack/thread.h>
#include <jack/statistics.h>

#include "config.h"
#include "tuneables.h"

#ifdef _WIN64
#include "asio64.h"
#else
#include "asio32.h"
#endif

#include "classid.h"
#include "jackasio.h"


WINE_DEFAULT_DEBUG_CHANNEL (asio);


/*****************************************************************************
 *      COM INTERFACE: IWineASIO
 */
typedef struct IWineASIO *LPWINEASIO;

#define INTERFACE IWineASIO
DECLARE_INTERFACE_(IWineASIO,IUnknown)
{
    STDMETHOD_ (HRESULT, QueryInterface) (THIS_
                                          IID    riid,
                                          void **ppvObject) PURE;
    STDMETHOD_ (ULONG, AddRef)           (THIS)             PURE;
    STDMETHOD_ (ULONG, Release)          (THIS)             PURE;

    STDMETHOD_ (ASIOBool, Init) (THIS_ void *sysHandle) PURE;

    STDMETHOD_ (void, GetDriverName)    (THIS_
                                         char *name)   PURE;
    STDMETHOD_ (LONG, GetDriverVersion) (THIS)         PURE;
    STDMETHOD_ (void, GetErrorMessage)  (THIS_
                                         char *string) PURE;

    STDMETHOD_ (ASIOError, Start) (THIS) PURE;
    STDMETHOD_ (ASIOError, Stop)  (THIS) PURE;

    STDMETHOD_ (ASIOError, GetChannels)   (THIS_
                                           LONG *numInpChans,
                                           LONG *numOutChans) PURE;
    STDMETHOD_ (ASIOError, GetLatencies)  (THIS_
                                           LONG *inputLatency,
                                           LONG *outputLatency)     PURE;
    STDMETHOD_ (ASIOError, GetBufferSize) (THIS_
                                           LONG *minSize,
                                           LONG *maxSize,
                                           LONG *preferredSize,
                                           LONG *granularity)       PURE;

    STDMETHOD_ (ASIOError, CanSampleRate) (THIS_
                                           ASIOSampleRate  sampleRate) PURE;
    STDMETHOD_ (ASIOError, GetSampleRate) (THIS_
                                           ASIOSampleRate *sampleRate) PURE;
    STDMETHOD_ (ASIOError, SetSampleRate) (THIS_
                                           ASIOSampleRate  sampleRate) PURE;

    STDMETHOD_ (ASIOError, GetClockSources) (THIS_
                                             ASIOClockSource *clocks,
                                             LONG            *numSources) PURE;
    STDMETHOD_ (ASIOError, SetClockSource)  (THIS_
                                             LONG             index)      PURE;

    STDMETHOD_ (ASIOError, GetSamplePosition) (THIS_
                                               ASIOSamples     *sPos,
                                               ASIOTimeStamp   *tStamp) PURE;
    STDMETHOD_ (ASIOError, GetChannelInfo)    (THIS_
                                               ASIOChannelInfo *info)   PURE;

    STDMETHOD_ (ASIOError, CreateBuffers)  (THIS_
                                            ASIOBufferInfo *bufInfo,
                                            LONG            numChans,
                                            LONG            bufSz,
                                            ASIOCallbacks  *asioCbs) PURE;
    STDMETHOD_ (ASIOError, DisposeBuffers) (THIS)                          PURE;

    STDMETHOD_ (ASIOError, ControlPanel) (THIS)           PURE;
    STDMETHOD_ (ASIOError, Future)       (THIS_
                                          LONG  selector,
                                          void *opt)      PURE;
    STDMETHOD_ (ASIOError, OutputReady)  (THIS)           PURE;
};
#undef INTERFACE


/******************************************************************************
 *      INSTANCE STRUCTURES & VARIABLES: Declarations
 */
typedef struct WineASIOChannel
{
    BOOL                         active;
    jack_default_audio_sample_t *audio_buf;
    char                         port_name[WINEASIO_MAX_NAME_LEN];
    jack_port_t                 *port;
    jack_port_id_t               port_id;
}
WineASIOChannel;

typedef struct WineASIOSelMsg
{
    BOOL is_sel_support;
    LONG query_rslt;
}
WineASIOSelMsg;

typedef enum WineASIODriverState
{
    Loaded,
    Initialized,
    Prepared,
    Running
}
WineASIODriverState;

typedef struct IWineASIOImpl
{
    /*
     * COM stuff.
     */
    const IWineASIOVtbl *lpVtbl;
          LONG           ref;

    /*
     * Driver version.
     */
    LONG wineasio_version;

    /*
     * User-configurable parameters.
     */
    LONG wineasio_num_ports[WINEASIO_IN_OUT];
    LONG wineasio_fixed_bufsz;
    LONG wineasio_preferd_bufsz;
    BOOL wineasio_conn_ports;
    BOOL wineasio_start_jack;

    /*
     * Structures and enums unique to WineASIO.
     */

    WineASIODriverState  wineasio_drv_state;
    WineASIOSelMsg       wineasio_sel_msg[kAsioNumMessageSelectors];
    WineASIOChannel     *wineasio_iochan[WINEASIO_IN_OUT];
    WineASIOChannel     *wineasio_iochan_curr;

    /*
     * The app's main window handle on windows; 0 on OS/X.
     */
    HWND sys_handle;

    /*
     * ASIO stuff.  The structure definitions here come from asio.h.
     */
    ASIOCallbacks   *asio_cbs;
    ASIOTime         asio_time;
    ASIOTimeStamp    asio_tstmp;
    ASIOSamples      asio_smpl_pos;
    ASIOSampleType   asio_smpl_fmt;
    ASIOSampleRate   asio_smpl_rate;
    LONG             asio_active[WINEASIO_IN_OUT]; // FIXME: should this be BOOL?
    LONG             asio_bufsz;
    BOOL             asio_buf_ndx;
    BOOL             asio_tcode_enabled;

    /*
     * JACK stuff.
     */
          jack_client_t                *jack_client;
          char                          jack_client_name[WINEASIO_MAX_NAME_LEN];
          int                           jack_num_phys_ports[WINEASIO_IN_OUT];
    const char                        **jack_phys_port[WINEASIO_IN_OUT];
          BOOL                          jack_ignore_port_registration;
          BOOL                          jack_ignore_port_connect;
          jack_default_audio_sample_t  *jack_audio_buf;
}
IWineASIOImpl;


/******************************************************************************\
 *      INTERFACE METHODS: Forward Declarations
 *
 *      As seen from the WineASIO source.
 *      We hide ELF symbols for the COM members; no need to to export them.
 */
#define HIDDEN DECLSPEC_HIDDEN

HIDDEN HRESULT   STDMETHODCALLTYPE QueryInterface (LPWINEASIO   iface,
                                                   REFIID       riid,
                                                   void       **ppvObject);
HIDDEN ULONG     STDMETHODCALLTYPE AddRef         (LPWINEASIO   iface);
HIDDEN ULONG     STDMETHODCALLTYPE Release        (LPWINEASIO   iface);

HIDDEN ASIOBool  STDMETHODCALLTYPE Init (LPWINEASIO  iface,
                                         void       *sysHandle);

HIDDEN void      STDMETHODCALLTYPE GetDriverName    (LPWINEASIO  iface,
                                                     char       *name);
HIDDEN LONG      STDMETHODCALLTYPE GetDriverVersion (LPWINEASIO  iface);
HIDDEN void      STDMETHODCALLTYPE GetErrorMessage  (LPWINEASIO  iface,
                                                     char       *string);

HIDDEN ASIOError STDMETHODCALLTYPE Start (LPWINEASIO iface);
HIDDEN ASIOError STDMETHODCALLTYPE Stop  (LPWINEASIO iface);

HIDDEN ASIOError STDMETHODCALLTYPE GetChannels   (LPWINEASIO  iface,
                                                  LONG       *numInpChans,
                                                  LONG       *numOutChans);
HIDDEN ASIOError STDMETHODCALLTYPE GetLatencies  (LPWINEASIO  iface,
                                                  LONG       *inputLatency,
                                                  LONG       *outputLatency);
HIDDEN ASIOError STDMETHODCALLTYPE GetBufferSize (LPWINEASIO  iface,
                                                  LONG       *minSize,
                                                  LONG       *maxSize,
                                                  LONG       *preferredSize,
                                                  LONG       *granularity);

HIDDEN ASIOError STDMETHODCALLTYPE CanSampleRate (LPWINEASIO      iface,
                                                  ASIOSampleRate  sampleRate);
HIDDEN ASIOError STDMETHODCALLTYPE GetSampleRate (LPWINEASIO      iface,
                                                  ASIOSampleRate *sampleRate);
HIDDEN ASIOError STDMETHODCALLTYPE SetSampleRate (LPWINEASIO      iface,
                                                  ASIOSampleRate  sampleRate);

HIDDEN ASIOError STDMETHODCALLTYPE GetClockSources (LPWINEASIO       iface,
                                                    ASIOClockSource *clocks,
                                                    LONG            *numSources);
HIDDEN ASIOError STDMETHODCALLTYPE SetClockSource  (LPWINEASIO       iface,
                                                    LONG             index);

HIDDEN ASIOError STDMETHODCALLTYPE GetSamplePosition (LPWINEASIO       iface,
                                                      ASIOSamples     *sPos,
                                                      ASIOTimeStamp   *tStamp);
HIDDEN ASIOError STDMETHODCALLTYPE GetChannelInfo    (LPWINEASIO       iface,
                                                      ASIOChannelInfo *info);

HIDDEN ASIOError STDMETHODCALLTYPE CreateBuffers  (LPWINEASIO      iface,
                                                   ASIOBufferInfo *bufInfo,
                                                   LONG            numChans,
                                                   LONG            bufSz,
                                                   ASIOCallbacks  *asioCbs);
HIDDEN ASIOError STDMETHODCALLTYPE DisposeBuffers (LPWINEASIO      iface);

HIDDEN ASIOError STDMETHODCALLTYPE ControlPanel (LPWINEASIO  iface);
HIDDEN ASIOError STDMETHODCALLTYPE Future       (LPWINEASIO  iface,
                                                 LONG        selector,
                                                 void       *opt);
HIDDEN ASIOError STDMETHODCALLTYPE OutputReady  (LPWINEASIO  iface);


/******************************************************************************
 *      INTERFACE METHODS: Thiscall wrappers for the vtbl
 *
 *      As seen from 32-bit app side.
 */
HIDDEN void __thiscall_Init (void);
HIDDEN void __thiscall_GetDriverName (void);
HIDDEN void __thiscall_GetDriverVersion (void);
HIDDEN void __thiscall_GetErrorMessage (void);
HIDDEN void __thiscall_Start (void);
HIDDEN void __thiscall_Stop (void);
HIDDEN void __thiscall_GetChannels (void);
HIDDEN void __thiscall_GetLatencies (void);
HIDDEN void __thiscall_GetBufferSize (void);
HIDDEN void __thiscall_CanSampleRate (void);
HIDDEN void __thiscall_GetSampleRate (void);
HIDDEN void __thiscall_SetSampleRate (void);
HIDDEN void __thiscall_GetClockSources (void);
HIDDEN void __thiscall_SetClockSource (void);
HIDDEN void __thiscall_GetSamplePosition (void);
HIDDEN void __thiscall_GetChannelInfo (void);
HIDDEN void __thiscall_CreateBuffers (void);
HIDDEN void __thiscall_DisposeBuffers (void);
HIDDEN void __thiscall_ControlPanel (void);
HIDDEN void __thiscall_Future (void);
HIDDEN void __thiscall_OutputReady (void);


/******************************************************************************
 *      COM INTERFACE METHOD: QueryInterface ()
 */
HIDDEN HRESULT STDMETHODCALLTYPE
QueryInterface (LPWINEASIO   iface,
                REFIID       riid,
                void       **ppvObject)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    LONG             result;

    TRACE ("iface: %p, riid: %s, ppvObject: %p)\n", iface,
                                                    debugstr_guid (riid),
                                                    ppvObject);

    if (ppvObject == NULL)
    {
        result = E_INVALIDARG;
        goto out;
    }

    if (!IsEqualIID (&CLSID_WineASIO, riid))
    {
        result = E_NOINTERFACE;
        goto out;
    }

    AddRef (iface);
    *ppvObject = This;

    result = S_OK;

out:
    return result;
}


/******************************************************************************
 *      COM INTEFACE METHOD: AddRef ()
 */
HIDDEN ULONG STDMETHODCALLTYPE
AddRef (LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ULONG            ref  = InterlockedIncrement (&(This->ref));

    TRACE ("iface: %p, ref count is %d\n", iface, ref);

    return ref;
}


/******************************************************************************
 *      COM INTERFACE METHOD: Release ()
 */
HIDDEN ULONG STDMETHODCALLTYPE
Release (LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ULONG            ref  = InterlockedDecrement (&This->ref);
    int              i;

    TRACE ("iface: %p, ref count is %d\n", iface,
                                           ref);

    // FIXME: do we need these tests here, or are they done in the function?
    if (This->wineasio_drv_state == Running)
    {
        Stop (iface);
    }

    if (This->wineasio_drv_state == Prepared)
    {
        DisposeBuffers (iface);
    }

    // FIXME refactor this
    if (This->wineasio_drv_state == Initialized)
    {
        /* just for good measure we deinitialize WineASIOChannel structures and unregister JACK ports */
        for (i = 0; i < This->wineasio_num_ports[WINEASIO_IN]; i++)
        {
            if (jack_port_unregister (This->jack_client,
                                      This->wineasio_iochan[WINEASIO_IN][i].port))
            {
                MESSAGE ("Error trying to unregister port %s\n",
                         This->wineasio_iochan[WINEASIO_IN][i].port_name);
            }

            This->wineasio_iochan[WINEASIO_IN][i].active = ASIOFalse;
            This->wineasio_iochan[WINEASIO_IN][i].port = NULL;
        }

        for (i = 0; i < This->wineasio_num_ports[WINEASIO_OUT]; i++)
        {
            if (jack_port_unregister (This->jack_client,
                                      This->wineasio_iochan[WINEASIO_OUT][i].port))
            {
                MESSAGE ("Error trying to unregister port %s\n",
                         This->wineasio_iochan[WINEASIO_OUT][i].port_name);
            }

            This->wineasio_iochan[WINEASIO_OUT][i].active = ASIOFalse;
            This->wineasio_iochan[WINEASIO_OUT][i].port = NULL;
        }
        This->asio_active[WINEASIO_IN] = This->asio_active[WINEASIO_OUT] = 0;
        TRACE ("%i WineASIOChannel structures released\n",
               This->wineasio_num_ports[WINEASIO_IN] + This->wineasio_num_ports[WINEASIO_OUT]);

        if (This->jack_phys_port[WINEASIO_OUT])
        {
            jack_free (This->jack_phys_port[WINEASIO_OUT]);
        }
        if (This->jack_phys_port[WINEASIO_IN])
        {
            jack_free (This->jack_phys_port[WINEASIO_IN]);
        }

        if (This->jack_client)
        {
            if (jack_client_close (This->jack_client))
            {
                MESSAGE ("Error trying to close JACK client\n");
            }
        }

        if (This->wineasio_iochan[WINEASIO_IN])
        {
            HeapFree (GetProcessHeap (), 0, This->wineasio_iochan[WINEASIO_IN]);
        }
    }

    if (ref == 0)
    {
        HeapFree (GetProcessHeap (), 0, This);
    }

    TRACE ("WineASIO terminated\n\n");

    return ref;
}


/******************************************************************************
 *      ARCH-INDEPENDENT 64-BIT UTILITY FUNCTIONS: Forward Declarations
 */
static long long int value_64_bit      (ASIOSamples   *val);
static void          copy_64_bit       (ASIOSamples   *var1,
                                        ASIOSamples   *var2);
static void          store_64_bit      (ASIOSamples   *var,
                                        long long int  val);
static void          add_64_clipped_32 (ASIOSamples   *var,
                                        DWORD          val);

/******************************************************************************
 *      ARCH-INDEPENDENT 64-BIT UTILITY FUNCTION: value_64_bit ()
 */
static long long int
value_64_bit (ASIOSamples *val)
{
    long long int   result;

#if NATIVE_INT64
    result = *val;
#else
    result = ((long long int)val->hi << 32) + (val->lo & 0xffffffff);
#endif

    return result;
}

/******************************************************************************
 *      ARCH-INDEPENDENT 64-BIT UTILITY FUNCTION: copy_64_bit ()
 */
static void
copy_64_bit (ASIOSamples *var1,
             ASIOSamples *var2)
{
#if NATIVE_INT64
    *var1 = *var2;
#else
    var1->hi = var2->hi;
    var1->lo = var2->lo;
#endif
}

/******************************************************************************
 *      ARCH-INDEPENDENT 64-BIT UTILITY FUNCTION: store_64_bit ()
 */
static void
store_64_bit (ASIOSamples   *var,
              long long int  val)
{
#if NATIVE_INT64
    *var = val;
#else
    var->hi = val >> 32;
    var->lo = val & 0xffffffff;
#endif
}

/******************************************************************************
 *      ARCH-INDEPENDENT 64-BIT UTILITY FUNCTION: add_64_clipped_32 ()
 */
static void
add_64_clipped_32 (ASIOSamples *var,
                   DWORD        val)
{
#if NATIVE_INT64
    var += val;
    val &= 0xffffffff;
#else
    var->lo += val;
    var->hi = 0;
#endif
}


/******************************************************************************
 *      JACK CALLBACKS: Forward Declarations
 */
static void port_registration_cb (jack_port_id_t  port,
                                  int             reg,
                                  void           *arg);
static void port_connect_cb      (jack_port_id_t  a,
                                  jack_port_id_t  b,
                                  int             connect,
                                  void           *arg);
static int  xrun_cb              (void           *arg);
static int  srate_cb             (jack_nframes_t  nframes,
                                  void           *arg);
static int  bufsize_cb           (jack_nframes_t  nframes,
                                  void           *arg);
static int  process_cb           (jack_nframes_t  nframes,
                                  void           *arg);

/******************************************************************************
 *      JACK CALLBACK SUPPORT FUNCTION: Forward Declaration
 */
static void send_reset_to_host (IWineASIOImpl *This);

/******************************************************************************
 *      JACK CALLBACK SUPPORT FUNCTION: send_reset_to_host ()
 */
static void
send_reset_to_host (IWineASIOImpl *This)
{
    if (This->wineasio_drv_state != Running || This->jack_ignore_port_connect)
    {
        goto out;
    }

    if (This->wineasio_sel_msg[kAsioResetRequest].is_sel_support)
    {
        TRACE ("Sending `AsioResetRequest' message to host\n");

        This->jack_ignore_port_connect = TRUE;

        This->asio_cbs->asioMessage (kAsioResetRequest, 0, 0, 0);
    }

out:
    return;
}

/******************************************************************************
 *      JACK CALLBACK: port_registration_cb ()
 */
static void
port_registration_cb (jack_port_id_t  port,
                      int             reg,
                      void           *arg)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) arg;

    TRACE ("port: %d, reg: %d, arg: %p\n", port, reg, arg);

    if (This->jack_ignore_port_registration || !This->wineasio_iochan_curr)
    {
        TRACE ("caught another JACK client's port registration; ignoring\n");
        goto out;
    }

    if (reg)
    {
        if (This->wineasio_iochan_curr->port_id != WINEASIO_NO_PORT_ID)
        {
            WINEASIO_INTERNAL_ERROR (0);
        }
        This->wineasio_iochan_curr->port_id = port;
    }
    else
    {
        if (This->wineasio_iochan_curr->port_id == WINEASIO_NO_PORT_ID)
        {
            WINEASIO_INTERNAL_ERROR (1);
        }
        This->wineasio_iochan_curr->port_id = WINEASIO_NO_PORT_ID;
        // FIXME: make sure this is initialized as -1 when its created.
    }

out:
    return;
}

/******************************************************************************
 *      JACK CALLBACK: port_connect_cb ()
 */
static void
port_connect_cb (jack_port_id_t  a,
                 jack_port_id_t  b,
                 int             connect,
                 void           *arg)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) arg;

    TRACE ("%sconnected port (%d) %s port (%d)\n",
           connect ? "" : "dis", a, connect ? "to" : "from", b);

    send_reset_to_host (This);

    return;
}
/******************************************************************************
 *      JACK CALLBACK: xrun_cb ()
 */
static int
xrun_cb (void *arg)
{
    IWineASIOImpl *This = (IWineASIOImpl *) arg;
    long long int pos;

    pos = value_64_bit (&This->asio_smpl_pos);
    WARN ("asio_smpl_pos: %lld\n", pos);

    return 0;
}

/******************************************************************************
 *      JACK CALLBACK: srate_cb ()
 */
static int
srate_cb (jack_nframes_t  nframes,
                void           *arg)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) arg;

    TRACE ("\n");

    if (This->wineasio_drv_state != Running)
    {
        goto out;
    }

    This->asio_smpl_rate = nframes;
    This->asio_cbs->sampleRateDidChange (nframes);

out:
    return 0;
}

/******************************************************************************
 *      JACK CALLBACK: bufsize_cb ()
 */
static int
bufsize_cb (jack_nframes_t  nframes,
            void           *arg)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) arg;

    TRACE ("\n");

    send_reset_to_host (This);

    return 0;
}

/******************************************************************************
 *      JACK CALLBACK process_cb () SUPPORT FUNCTIONS:  Forward Declarations
 */
static void          copy_asio_to_jack (IWineASIOImpl  *This,
                                        jack_nframes_t  nframes);
static long long int get_nano_secs     (void);
static void          do_buffer_switch  (IWineASIOImpl  *This,
                                        long long int   smpl_pos);
static void          copy_jack_to_asio (IWineASIOImpl  *This,
                                        jack_nframes_t  nframes);

/******************************************************************************
 *      JACK CALLBACK process_cb () SUPPORT FUNCTION: copy_asio_to_jack ()
 */
static void
copy_asio_to_jack (IWineASIOImpl  *This,
                   jack_nframes_t  nframes)
{
    int i;
    int j;
    jack_default_audio_sample_t *out;

    for (i = 0; i < This->asio_active[WINEASIO_OUT]; i++)
    {
        if (This->wineasio_iochan[WINEASIO_OUT][i].active)
        {
            out = jack_port_get_buffer (This->wineasio_iochan[WINEASIO_OUT][i].port, nframes);

            switch (This->asio_smpl_fmt)
            {
                case ASIOSTInt32LSB:
                    for (j = 0; j < nframes; j++)
                    {
                        out[j] = ((int *) This->wineasio_iochan[WINEASIO_OUT][i]
                            .audio_buf)[nframes * This->asio_buf_ndx + j]
                            / (float) 0x7fffffff;
                    }
                    break;

                case ASIOSTFloat32LSB:
                    memcpy (out,
                            &This->wineasio_iochan[WINEASIO_OUT][i].audio_buf[nframes
                                * This->asio_buf_ndx],
                            sizeof (jack_default_audio_sample_t) * nframes);
                    break;

                default:
                    ERR ("Unknown sample format!");
                    break;
            }
        }
    }

    return;
}

/******************************************************************************
 *      JACK CALLBACK process_cb () SUPPORT FUNCTION: get_nano_secs ()
 */
static long long int
get_nano_secs (void)
{
    DWORD           tgt;
    long long int   t;

    tgt = timeGetTime ();
    t = (long long int)tgt * 1000000;

    return t;
}

/******************************************************************************
 *      JACK CALLBACK process_cb () SUPPORT FUNCTION: do_buffer_switch ()
 */
static void
do_buffer_switch (IWineASIOImpl *This,
                 long long int  smpl_pos)
{
    long long int nano_secs;

    nano_secs = get_nano_secs ();

    store_64_bit ((ASIOSamples *)&This->asio_tstmp, nano_secs);
    store_64_bit (&This->asio_smpl_pos, smpl_pos);

    if (This->wineasio_sel_msg[kAsioSupportsTimeInfo].query_rslt)
    {
         copy_64_bit ((ASIOSamples *)&This->asio_time.timeInfo.systemTime,
                      (ASIOSamples *)&This->asio_tstmp);
        store_64_bit (&This->asio_time.timeInfo.samplePosition, smpl_pos);
        This->asio_time.timeInfo.sampleRate = This->asio_smpl_rate;
        This->asio_time.timeInfo.speed = 1.0;
        This->asio_time.timeInfo.flags = kSystemTimeValid
                                       | kSamplePositionValid
                                       | kSampleRateValid
                                       | kSpeedValid;
        if (This->asio_tcode_enabled)
        {
            copy_64_bit ((ASIOSamples *)&This->asio_time.timeCode.timeCodeSamples,
                         (ASIOSamples *)&This->asio_tstmp);
            This->asio_time.timeCode.flags = ~(kTcValid | kTcRunning);
        }

        This->asio_cbs->bufferSwitchTimeInfo (&This->asio_time,
                                              This->asio_buf_ndx,
                                              ASIOFalse);
    }
    else
    {
        This->asio_cbs->bufferSwitch (This->asio_buf_ndx, ASIOFalse);
    }

    return;
}

/******************************************************************************
 *      JACK CALLBACK process_cb () SUPPORT FUNCTION: copy_jack_to_asio ()
 */
static void
copy_jack_to_asio (IWineASIOImpl  *This,
                   jack_nframes_t  nframes)
{
    int i;
    int j;
    jack_default_audio_sample_t *in;

    for (i = 0; i < This->asio_active[WINEASIO_IN]; i++)
    {
        if (This->wineasio_iochan[WINEASIO_IN][i].active)
        {
            in = jack_port_get_buffer (This->wineasio_iochan[WINEASIO_IN][i].port, nframes);

            switch (This->asio_smpl_fmt)
            {
                case ASIOSTInt32LSB:
                    for (j = 0; j < nframes; j++)
                    {
                        ((int *)This->wineasio_iochan[WINEASIO_IN][i].audio_buf)[nframes
                            * This->asio_buf_ndx + j] = in[j] * 0x7fffffff;
                    }
                    break;

                case ASIOSTFloat32LSB:
                    memcpy (&This->wineasio_iochan[WINEASIO_IN][i].audio_buf[nframes
                                * This->asio_buf_ndx],
                            in, sizeof (jack_default_audio_sample_t) * nframes);
                    break;

                default:
                    ERR ("Unknown sample type!\n");
                    break;
            }
        }
    }

    return;
}

/******************************************************************************
 *      JACK CALLBACK: process_cb ()
 */
static int
process_cb (jack_nframes_t  nframes,
                  void           *arg)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) arg;
    long long int    pos;

    copy_jack_to_asio (This, nframes);

    add_64_clipped_32 (&This->asio_smpl_pos, nframes);

    pos = value_64_bit (&This->asio_smpl_pos);

    do_buffer_switch (This, pos);

    copy_asio_to_jack (This, nframes);

    This->asio_buf_ndx = This->asio_buf_ndx == BUF_B ? BUF_A : BUF_B;

    return 0;
}


/******************************************************************************
 *      JACK THREAD CREATOR thread_creator_cb (): Forward Declarations
 */
static DWORD WINAPI thread_creator_cb_helper (      LPVOID          arg);

static int          thread_creator_cb        (      pthread_t      *thread_id,
                                              const pthread_attr_t *attr,
                                                    void           *(*fn)(void*),
                                                    void           *arg);

/******************************************************************************
 *      thread_creator_cb () SUPPORT FUNCTION: thread_creator_cb_helper ()
 *
 * Internal helper function for returning the posix thread_id of the newly
 * created callback thread
 */
struct
{
    void      *(*cb_thread) (void*);
    void      *arg;
    pthread_t  cb_pthread;
    HANDLE     cb_thread_created;
}
thread_creator;

static DWORD WINAPI
thread_creator_cb_helper (LPVOID arg)
{
    TRACE("arg: %p\n", arg);

    thread_creator.cb_pthread = pthread_self();
    SetEvent(thread_creator.cb_thread_created);
    thread_creator.cb_thread (thread_creator.arg);

    return 0;
}

/******************************************************************************
 *      JACK THREAD CREATOR: thread_creator_cb ()
 *
 * Function called by JACK to create a thread in the wine process context,
 * uses the global structure thread_creator to communicate
 * with jack_thread_creator_cb_helper().
 */
static int
thread_creator_cb (pthread_t            *thread_id,
                   const pthread_attr_t *attr,
                   void                 *(*function)(void*),
                   void                 *arg)
{
    struct sched_param priority_99 = { 99 };
    int                ret;

    TRACE ("arg: %p, thread_id: %p, attr: %p, function: %p\n",
           arg, thread_id, attr, function);

    thread_creator.cb_thread = function;
    thread_creator.arg = arg;
    thread_creator.cb_thread_created = CreateEventW (NULL, FALSE, FALSE, NULL);

    CreateThread (NULL, 0, thread_creator_cb_helper, arg, 0, 0);
    WaitForSingleObject (thread_creator.cb_thread_created, INFINITE);

    *thread_id = thread_creator.cb_pthread;

    ret = pthread_setschedparam (*thread_id, SCHED_FIFO, &priority_99);
    if (ret)
    {
        ERR ("failed to set realtime priority on JACK thread, ret: %d\n", ret);
    }
    else
    {
        TRACE ("set realtime priority on JACK thread\n");
    }

    return 0;
}


/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTIONS: Forward Declarations
 */
static void      register_ports1         (      IWineASIOImpl *This);
static BOOL      register_jack_callbacks (      IWineASIOImpl *This);
static void      count_phys_ports        (      IWineASIOImpl *This);
static ASIOError alloc_iochans           (      IWineASIOImpl *This);
static ASIOError get_jack_params         (      IWineASIOImpl *This);
static ASIOError open_jack_client        (      IWineASIOImpl *This);
static void      sanitize_prefer_bufsz   (      IWineASIOImpl *This);
static void      get_client_name_param   (      IWineASIOImpl *This);
static void      get_user_param          (      IWineASIOImpl *This,
                                          const char          *lp_env_name,
                                                HKEY           hkey,
                                          const WCHAR         *reg_value_name,
                                                LONG          *impl_var);
static HKEY      create_reg_key          (      void);
static void      get_all_user_params     (      IWineASIOImpl *This);
static void      init_impl_defaults      (      IWineASIOImpl *This,
                                                void          *sys_handle);

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: register_jack_callbacks ()
 */
static BOOL
register_jack_callbacks (IWineASIOImpl *This)
{
    BOOL    ret;

    jack_set_thread_creator (thread_creator_cb);

    if (jack_set_process_callback (This->jack_client, process_cb, This))
    {
        ERR ("Unable to register JACK process callback\n");
        goto err_out;
    }

    if (jack_set_buffer_size_callback (This->jack_client,
                                       bufsize_cb,
                                       This))
    {
        ERR ("Unable to register JACK buffersize change callback\n");
        goto err_out;
    }

    if (jack_set_sample_rate_callback (This->jack_client,
                                       srate_cb,
                                       This))
    {
        ERR ("Unable to register JACK samplerate change callback\n");
        goto err_out;
    }

    if (jack_set_xrun_callback (This->jack_client, xrun_cb, This))
    {
        ERR ("Unable to register JACK XRUN callback\n");
        goto err_out;
    }

    if (jack_set_port_connect_callback (This->jack_client, port_connect_cb, This))
    {
        ERR ("Unable to register JACK port connect/disconnect callback\n");
        goto err_out;
    }

    if (jack_set_port_registration_callback (This->jack_client,
                                             port_registration_cb, This))
    {
        ERR ("Unable to register JACK port registration/deregistration callback\n");
        goto err_out;
    }

    ret = TRUE;

out:
    return ret;

err_out:
    ret = FALSE;
    goto out;
}

#if 0
/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: deregister_ports ()
 */
static void
deregister_ports (IWineASIOImpl *This)
{
    LONG    i;

    for (i = 0; i < This->wineasio_num_ports[WINEASIO_IN]; i++)
    {
        This->wineasio_iochan[WINEASIO_IN][i].port_name[0] = 0;
        jack_port_unregister (This->jack_client, This->wineasio_iochan[WINEASIO_IN][i].port);
        This->wineasio_iochan[WINEASIO_IN][i].port = NULL;
    }

    for (i = 0; i < This->wineasio_num_ports[WINEASIO_OUT]; i++)
    {
        This->wineasio_iochan[WINEASIO_OUT][i].port_name[0] = 0;
        jack_port_unregister (This->jack_client, This->wineasio_iochan[WINEASIO_OUT][i].port);
        This->wineasio_iochan[WINEASIO_OUT][i].port = NULL;
    }

    return;
}
#endif

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: register_ports1 ()
 */
static void
register_ports1 (IWineASIOImpl *This)
{
    LONG    i;

    for (i = 0; i < This->wineasio_num_ports[WINEASIO_IN]; i++)
    {
        This->wineasio_iochan[WINEASIO_IN][i].active  = FALSE;
    }

    for (i = 0; i < This->wineasio_num_ports[WINEASIO_OUT]; i++)
    {
        This->wineasio_iochan[WINEASIO_OUT][i].active  = FALSE;
    }

    TRACE ("%i in+out WineASIOChannel structs inited FALSE\n",
           This->wineasio_num_ports[WINEASIO_IN] + This->wineasio_num_ports[WINEASIO_OUT]);

    return;
}

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: free_phys_ports ()
 */
static void
free_phys_ports (IWineASIOImpl *This)
{
    jack_free (This->jack_phys_port[WINEASIO_IN]);
    This->jack_num_phys_ports[WINEASIO_IN] = 0;

    jack_free (This->jack_phys_port[WINEASIO_OUT]);
    This->jack_num_phys_ports[WINEASIO_OUT] = 0;

    return;
}

static void
free_iochans (IWineASIOImpl *This)
{
    HeapFree (GetProcessHeap (), 0, This->wineasio_iochan[WINEASIO_IN]);

    This->wineasio_iochan[WINEASIO_IN]  = NULL;
    This->wineasio_iochan[WINEASIO_OUT] = NULL;

    return;
}

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: alloc_iochans ()
 */
static ASIOError
alloc_iochans (IWineASIOImpl *This)
{
    LONG        total_ports;
    ASIOError   result;

    total_ports = This->wineasio_num_ports[WINEASIO_IN]
                + This->wineasio_num_ports[WINEASIO_OUT];

    This->wineasio_iochan[WINEASIO_IN] =
        HeapAlloc (GetProcessHeap (), 0, total_ports * sizeof (WineASIOChannel));

    if (!This->wineasio_iochan[WINEASIO_IN])
    {
        ERR ("Unable to allocate %i WineASIOChannel structures\n", total_ports);
        result = ASIOFalse;
        goto out;
    }

    This->wineasio_iochan[WINEASIO_OUT] = This->wineasio_iochan[WINEASIO_IN]
        + This->wineasio_num_ports[WINEASIO_IN];

    TRACE ("%i WineASIOChannel structures allocated\n", total_ports);

    result = ASIOTrue;

out:
    return result;
}

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: count_phys_ports ()
 */
static void
count_phys_ports (IWineASIOImpl *This)
{
    const char **foo;
    int i;

    foo = jack_get_ports (This->jack_client, NULL, NULL, 0);

    for (i = 0; foo[i]; i++)
    {
        TRACE ("XXXXXXX `%s'\n", foo[i]);
    }

    This->jack_phys_port[WINEASIO_IN] = jack_get_ports (This->jack_client,
                                                NULL, NULL,
                                                JackPortIsPhysical
                                                    | JackPortIsOutput);
    for (This->jack_num_phys_ports[WINEASIO_IN] = 0;
         This->jack_phys_port[WINEASIO_IN]
            && This->jack_phys_port[WINEASIO_IN][This->jack_num_phys_ports[WINEASIO_IN]];
         This->jack_num_phys_ports[WINEASIO_IN]++)
    {
        /*
         * Do nothing; just count.
         */
    }

    This->jack_phys_port[WINEASIO_OUT] = jack_get_ports (This->jack_client,
                                                NULL, NULL,
                                                JackPortIsPhysical
                                                    | JackPortIsInput);
    for (This->jack_num_phys_ports[WINEASIO_OUT] = 0;
         This->jack_phys_port[WINEASIO_OUT]
            && This->jack_phys_port[WINEASIO_OUT][This->jack_num_phys_ports[WINEASIO_OUT]];
         This->jack_num_phys_ports[WINEASIO_OUT]++)
    {
        /*
         * Do nothing; just count.
         */
    }

    TRACE ("counted %d inp and %d out phys ports\n",
           This->jack_num_phys_ports[WINEASIO_IN],
           This->jack_num_phys_ports[WINEASIO_OUT]);

    return;
}

static ASIOError
get_jack_params (IWineASIOImpl *This)
{
    ASIOError   result;

    This->asio_smpl_rate = jack_get_sample_rate (This->jack_client);
    if (!This->asio_smpl_rate)
    {
        ERR ("Unable to get samplerate from JACK\n");
        goto err_out;
    }
    TRACE ("JACK sample rate: %f\n", This->asio_smpl_rate);

    This->asio_bufsz = jack_get_buffer_size (This->jack_client);
    if (!This->asio_bufsz)
    {
        ERR ("Unable to get buffer size from JACK\n");
        goto err_out;
    }
    TRACE ("JACK buffer size: %d\n", This->asio_bufsz);

    result = ASIOTrue;

out:
    return result;

err_out:
    result = ASIOFalse;
    goto out;
}

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: open_jack_client ()
 */
static ASIOError
open_jack_client (IWineASIOImpl *This)
{
    jack_options_t  jack_options = JackNullOption;
    jack_status_t   jack_status;
    ASIOError       result;

    if (!This->wineasio_start_jack)
    {
        jack_options |= JackNoStartServer;
    }

    This->jack_client = jack_client_open (This->jack_client_name,
                                          jack_options,
                                          &jack_status);
    if (!This->jack_client)
    {
        ERR ("Unable to open a JACK client as: '%s'\n", This->jack_client_name);
        MessageBoxA (This->sys_handle,
                    "\nWineASIO initialization failed.\n\n"
                      "(Has JACK been started?)\n",
                    NULL,
                    MB_OK | MB_ICONERROR);
        result = ASIOFalse;
        goto out;
    }

    TRACE ("JACK client opened as: '%s'\n",
           jack_get_client_name (This->jack_client));

    result = ASIOTrue;

out:
    return result;
}

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: sanitize_prefer_bufsz ()
 */
static void
sanitize_prefer_bufsz (IWineASIOImpl *This)
{
    /*
     * If wineasio_preferd_bufsz is not a power of two or if out of range,
     * then set to DEFAULT_WINEASIO_PREFER_BUFSZ.
     */
    // FIXME: is the first test bogus?
    if (!(!(This->wineasio_preferd_bufsz & (This->wineasio_preferd_bufsz - 1))
          && This->wineasio_preferd_bufsz >= WINEASIO_MIN_BUFSZ
          && This->wineasio_preferd_bufsz <= WINEASIO_MAX_BUFSZ))
    {
        TRACE ("This->wineasio_preferd_bufsz: %d (sanitized)\n",
               DEFAULT_WINEASIO_PREFERD_BUFSZ);

        This->wineasio_preferd_bufsz = DEFAULT_WINEASIO_PREFERD_BUFSZ;

        // FIXME: do we want to write this to the registry?
    }

    return;
}

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: get_user_client_name ()
 */
static void
get_client_name_param (IWineASIOImpl *This)
{
    char    lp_buffer[WINEASIO_MAX_NAME_LEN];
    DWORD   size;
    WCHAR   app_path[MAX_PATH];
    WCHAR  *app_name;
    WCHAR  *p;

    /*
     * Take the client name from the environment before
     * querying the application name.
     */
    size = GetEnvironmentVariableA ("WINEASIO_CLIENT_NAME",
                                    lp_buffer,
                                    WINEASIO_MAX_NAME_LEN);
    if (size && size < WINEASIO_MAX_NAME_LEN)
    {
        strcpy (This->jack_client_name, lp_buffer);
        goto out;
    }
    else
    {
        WARN ("envvar not found or value exceeds %d chars; ignoring\n",
              WINEASIO_MAX_NAME_LEN - 1);
    }

    GetModuleFileNameW (0, app_path, MAX_PATH);

    app_name = strrchrW (app_path, L'.');
    *app_name = 0;

    app_name = strrchrW (app_path, L'\\');
    app_name++;

    for (p = app_name; *p != 0; p++)
    {
        *p = tolowerW (*p);
    }

    WideCharToMultiByte (CP_ACP, WC_SEPCHARS, app_name, -1,
                         This->jack_client_name, WINEASIO_MAX_NAME_LEN,
                         NULL, NULL);
out:
    TRACE ("This->jack_client_name: '%s'\n", This->jack_client_name);

    return;
}

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: get_user_param ()
 */
// FIXME: separate this into 2 functions
static void
get_user_param (      IWineASIOImpl *This,
                const char          *lp_env_name,
                      HKEY           hkey,
                const WCHAR         *reg_value_name,
                      LONG          *impl_var)
{

    char    lp_buffer[WINEASIO_MAX_ENV_LEN];
    DWORD   rc;
    LONG    result;
    BOOL    got_env;
    BOOL    write_reg;
    DWORD   size;
    DWORD   type;
    LONG    value;

    TRACE("This: %p, lp_env_name: %p\n", This, lp_env_name); // FIXME

    rc = GetEnvironmentVariableA (lp_env_name,
                                  lp_buffer,
                                  WINEASIO_MAX_ENV_LEN);
    if (rc && rc < WINEASIO_MAX_ENV_LEN)
    {
        errno = 0;
        *impl_var = strtol (lp_buffer, NULL, 10);
        if (errno != ERANGE)
        {
            got_env = TRUE;
        }
        else
        {
            WARN ("range exceeded on envvar %s=strtol('%s'); ignoring value\n",
                  lp_env_name, lp_buffer);
            got_env = FALSE;
        }
    }
    else
    {
        WARN ("envvar '%s' not found or value exceeds %d chars; ignoring\n",
              lp_env_name, WINEASIO_MAX_ENV_LEN - 1);
        got_env = FALSE;
    }

    /*
     * If for some reason we couldn't create the registry key,
     * `hkey' will be NULL and we'll settle for the default value
     * passed in `impl_var' then exit this function without reading
     * the registry.
     */

    if (!hkey)
    {
        goto out;
    }

    write_reg = TRUE;

    size = sizeof (DWORD);

    if (RegQueryValueExW (hkey, reg_value_name, NULL,
                          &type, (LPBYTE) &value, &size) == ERROR_SUCCESS)
    {
        if (type == REG_DWORD)
        {
            if (!got_env)
            {
                if (*impl_var == value)
                {
                    write_reg = FALSE;
                }
                else
                {
                    *impl_var = value;
                }
            }
            else
            {
                if (*impl_var == value)
                {
                    write_reg = FALSE;
                }
            }
        }
        else
        {
            WARN ("Unexpected type %d in registry\n", type);
        }
    }

    if (write_reg)
    {
        value = *impl_var;
        result = RegSetValueExW (hkey, reg_value_name, 0,
                                 REG_DWORD, (LPBYTE) &value, sizeof (DWORD));
        if (result != ERROR_SUCCESS)
        {
            WARN ("RegSetValueExW failed with code %d\n", result);
        }
    }

out:
    return;
}

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: create_reg_key ()
 */
static const WCHAR key_software_wine_wineasio[] =
{
    'S','o','f','t','w','a','r','e','\\',
    'W','i','n','e','\\',
    'W','i','n','e','A','S','I','O',0
};

static HKEY
create_reg_key (void)
{
    LONG    rc;
    HKEY    hkey;

    rc = RegCreateKeyExW (HKEY_CURRENT_USER, key_software_wine_wineasio, 0,
                          NULL,
                          REG_OPTION_NON_VOLATILE,
                          KEY_ALL_ACCESS, NULL,
                          &hkey, NULL);

    if (rc != ERROR_SUCCESS)
    {
        ERR ("Unable to create registry key, code %d (skipping registry refs in init\n",
             rc);
        hkey = NULL;
    }

    return hkey;
}

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: get_all_user_params ()
 */
typedef struct
{
    const char  *env_var;
    const WCHAR *reg_value;
    LONG        *impl_var;
    const char  *str;
}
init_driver_tbl;

static const WCHAR value_wineasio_num_ports[WINEASIO_IN_OUT][18] =
{
    {
        'N','u','m','b','e','r',' ','o','f',' ','i','n','p','u','t','s',0
    },
    {
        'N','u','m','b','e','r',' ','o','f',' ','o','u','t','p','u','t','s',0
    }
};
static const WCHAR value_wineasio_fixed_bufsz[] =
{
    'I','s',' ','f','i','x','e','d',' ','b','u','f','f','e','r','s','i','z','e',0
};
static const WCHAR value_wineasio_preferd_bufsz[] =
{
    'P','r','e','f','e','r','r','e','d',' ','b','u','f','f','e','r','s','i','z','e',0
};
static const WCHAR value_wineasio_start_jack[] =
{
    'S','t','a','r','t',' ','s','e','r','v','e','r',0
};
static const WCHAR value_wineasio_conn_ports[] =
{
    'C','o','n','n','e','c','t',' ','t','o',' ','p','o','r','t','s',0
};

static void
get_all_user_params (IWineASIOImpl *This)
{
    LONG            i;
    HKEY            hkey;
    init_driver_tbl tbl[] =
    {
        {
            "WINEASIO_NUM_PORTS_INP", value_wineasio_num_ports[WINEASIO_IN],
            &This->wineasio_num_ports[WINEASIO_IN], "wineasio_num_ports[WINEASIO_IN]"
        },
        {
            "WINEASIO_NUM_PORTS_OUT", value_wineasio_num_ports[WINEASIO_OUT],
            &This->wineasio_num_ports[WINEASIO_OUT], "wineasio_num_ports[WINEASIO_OUT]"
        },
        {
            "WINEASIO_FIXED_BUFSZ", value_wineasio_fixed_bufsz,
            &This->wineasio_fixed_bufsz, "wineasio_fixed_bufsz"
        },
        {
            "WINEASIO_PREFERD_BUFSZ", value_wineasio_preferd_bufsz,
            &This->wineasio_preferd_bufsz, "wineasio_preferd_bufsz"
        },
        {
            "WINEASIO_CONN_PORTS", value_wineasio_conn_ports,
            &This->wineasio_conn_ports, "wineasio_conn_ports"
        },
        {
            "WINEASIO_START_JACK", value_wineasio_start_jack,
            &This->wineasio_start_jack, "wineasio_start_jack"
        },
        {
            NULL
        }
    };

    hkey = create_reg_key ();

    for (i = 0; tbl[i].env_var; i++)
    {
        TRACE ("This->%s: %d\n", tbl[i].str, *tbl[i].impl_var);

        get_user_param ( This, tbl[i].env_var, hkey,
                         tbl[i].reg_value, tbl[i].impl_var);

        TRACE ("This->%s: %d\n", tbl[i].str, *tbl[i].impl_var);
    }

    if (hkey)
    {
        RegCloseKey (hkey);
    }

    get_client_name_param (This);

    sanitize_prefer_bufsz (This);

    return;
}

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: init_impl_defaults ()
 */
static void
init_impl_defaults (IWineASIOImpl *This,
                    void          *sysHandle)
{
    /*
     * The version of our driver as a `nnn' formatted number,
     * which will be translated to `n.n.n'.
     */
    This->wineasio_version = WINEASIO_VERSION;

    /*
     * User-configurable parameters by way of the environment,
     * the registry, or hard-coded in `tuneables.h'.
     */
    This->wineasio_num_ports[WINEASIO_IN]  = DEFAULT_WINEASIO_NUM_PORTS_INP;
    This->wineasio_num_ports[WINEASIO_OUT] = DEFAULT_WINEASIO_NUM_PORTS_OUT;
    This->wineasio_fixed_bufsz             = DEFAULT_WINEASIO_FIXED_BUFSZ;
    This->wineasio_preferd_bufsz           = DEFAULT_WINEASIO_PREFERD_BUFSZ;
    This->wineasio_conn_ports              = DEFAULT_WINEASIO_CONN_PORTS;
    This->wineasio_start_jack              = DEFAULT_WINEASIO_START_JACK;

    This->wineasio_drv_state            = Loaded;
 /* This->wineasio_sel_msg              = { NULL }; */
    This->wineasio_iochan[WINEASIO_IN]  = NULL;
    This->wineasio_iochan[WINEASIO_OUT] = NULL;
    This->wineasio_iochan_curr          = NULL;

    /*
     * The app's main window handle on windows; 0 on OS X.
     */
    This->sys_handle = sysHandle;

    /*
     * ASIO stuff.
     */
    This->asio_cbs                  = NULL;
 /* This->asio_time                 = ... */
 /* This->asio_tstmp                = ... */
 /* This->asio_smpl_pos             = ... */
    This->asio_smpl_fmt             = WINEASIO_SMPL_FMT;
    This->asio_smpl_rate            = 0;
    This->asio_active[WINEASIO_IN]  = 0;
    This->asio_active[WINEASIO_OUT] = 0;
    This->asio_bufsz                = 0;
    This->asio_buf_ndx              = BUF_A;
    This->asio_tcode_enabled        = FALSE;

    /*
     * JACK stuff.
     */
    This->jack_client                       = NULL;
    This->jack_client_name[0]               = 0;
    This->jack_num_phys_ports[WINEASIO_IN]  = 0;
    This->jack_num_phys_ports[WINEASIO_OUT] = 0;
 /* This->jack_phys_port[WINEASIO_IN]       = ... */
 /* This->jack_phys_port[WINEASIO_OUT]      = ... */
    This->jack_ignore_port_registration     = TRUE;
    This->jack_ignore_port_connect          = FALSE;
    This->jack_audio_buf                    = NULL;

    return;
}

static void
init_iochans (IWineASIOImpl *This)
{
    return;
}

static ASIOBool
setup_iochans (IWineASIOImpl *This)
{
    ASIOBool    result;

    result = alloc_iochans (This);
    if (!result)
    {
        goto out;
    }

    init_iochans (This);

    result = ASIOTrue;

out:
    return result;
}

/******************************************************************************
 *      ASIO INTERFACE METHOD: Init ()
 *
 *      If this function returns `ASIOError', then the Windows host application
 *      will probably display a message saying it couldn't initialize WineASIO
 *      and that will be that.  A common reason for this happening is the
 *      user's failure to start the JACK server before selecting the WineASIO
 *      driver in the host application. FIXME
 */
DEFINE_THISCALL_WRAPPER (Init,8)
HIDDEN ASIOBool STDMETHODCALLTYPE
Init (LPWINEASIO  iface,
      void       *sysHandle)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ASIOBool         result;

    TRACE ("iface: %p, sysHandle: %p\n", iface, sysHandle);

    /*
     * Lock all pages which are (or will become) mapped into our address space
     * including new pages required by a growing heap and stack as well as
     * new memory-mapped files and shared memory regions.  [Linux function]
     */
    mlockall (MCL_FUTURE);

    /*
     * Initialize our implementation's instance variables with default values.
     */
    init_impl_defaults (This, sysHandle);

    /*
     * Get user-configurable parameters from environment variables
     * or failing that, the registry.  In the absense of these two
     * sources, use the defaults set in `init_impl_defaults' above.
     */
    get_all_user_params (This);

    /*
     * Open the JACK client.  If this fails, displays a message box asking the
     * user to check if JACK is running, then returns failure.
     *
     * If this happens, the user may be unable to start the JACK server while
     * the current WINE session is running because WINE may have grabbed the
     * ALSA device that JACK wants exclusive access to.  The solution, although
     * not pretty, is to quit WINE, start JACK, and then restart WINE.
     */
    result = open_jack_client (This);
    if (!result)
    {
        goto err_out;
    }

    /*
     * Get the JACK sample rate and buffer size and store it in the
     * instance variables for our implementation.  If this fails, close
     * the client and return failure.
     */
    result = get_jack_params (This);
    if (!result)
    {
        goto err_client_close;
    }

    /*
     * Count the number of physical input and output ports and store a pointer
     * to each input and output port structure.
     */
    count_phys_ports (This); // FIXME where do we jack_free() these structures?

    /*
     * Allocate an array of `n' WineASIOChannel structures, where `n' is
     * `num_ins' + `num_outs'.
     */
    result = setup_iochans (This);
    if (!result)
    {
        goto err_free_ports;
    }

    /*
     * Register each JACK callback function that we will use to: create real-
     * time threads, and monitor JACK parameter changes and status.  If any one
     * of the registrations fail, return failure.
     */
    result = register_jack_callbacks (This);
    if (!result)
    {
        goto err_free_iochans;
    }

    /*
     * Register each inp and out port of the client and initialize its
     * associated WineASIOChannel structure as being `inactive', with a textual
     * `portname' (e.g. `in_1', `in_2', etc.), and a pointer to the registered
     * port.
     */
    register_ports1 (This);

    /*
     * We're initialized!
     */
    SET_NEW_STATE_TRACE (Initialized, "Initialized");

    result = ASIOTrue;

out:
    FUNC_COMPLETION_TRUE_TRACE (result);
    return result;

err_free_iochans:
    free_iochans (This);

err_free_ports:
    free_phys_ports (This);

err_client_close:
    jack_client_close (This->jack_client);

err_out:
    result = ASIOFalse;
    goto out;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: GetDriverName ()
 */
DEFINE_THISCALL_WRAPPER (GetDriverName,8)
HIDDEN void STDMETHODCALLTYPE
GetDriverName (LPWINEASIO  iface,
               char       *name)
{
    TRACE ("iface: %p, name: %p\n", iface, name);

    strcpy (name, "Wine ASIO");

    return;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: GetDriverVersion ()
 */
DEFINE_THISCALL_WRAPPER (GetDriverVersion,4)
HIDDEN LONG STDMETHODCALLTYPE
GetDriverVersion (LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;

    TRACE ("iface: %p\n", iface);

    return This->wineasio_version;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: GetErrorMessage ()
 */
DEFINE_THISCALL_WRAPPER (GetErrorMessage,8)
HIDDEN void STDMETHODCALLTYPE
GetErrorMessage (LPWINEASIO  iface,
                 char       *string)
{
    TRACE ("iface: %p, string: %p)\n", iface, string);

    strcpy (string, "WineASIO does not return error messages\n");

    return;
}


/******************************************************************************
 *      ASIO Start() SUPPORT FUNCTIONS: FORWARD DECLARATIONS
 */
static void
zero_audio_buffer (IWineASIOImpl *This);

/******************************************************************************
 *      ASIO Init () SUPPORT FUNCTION: register_ports ()
 */
static void
register_ports (IWineASIOImpl *This,
                int            dir)
{

    int            i;
    const char    *name;
    unsigned long  flags;

    name  = (dir == WINEASIO_IN) ? "in"            : "out";
    flags = (dir == WINEASIO_IN) ? JackPortIsInput : JackPortIsOutput;

    // FIXME consider using Terminal flag in above

    for (i = 0; i < This->wineasio_num_ports[dir]; i++)
    {
        snprintf (This->wineasio_iochan[dir][i].port_name,
                  WINEASIO_MAX_NAME_LEN, "%s_%i", name, i + 1);

        This->wineasio_iochan_curr          = &This->wineasio_iochan[dir][i];
        This->jack_ignore_port_registration = FALSE;

        // Race could happen here. FIXME

        This->wineasio_iochan[dir][i].port
            = jack_port_register (This->jack_client,
                                  This->wineasio_iochan[dir][i].port_name,
                                  JACK_DEFAULT_AUDIO_TYPE, flags, 0);

        // Or here. FIXME
        // And the following has to be in this order.

        This->jack_ignore_port_registration = TRUE;
        This->wineasio_iochan_curr          = NULL;
    }

    TRACE ("(%i) `%s' ports registered\n", This->wineasio_num_ports[dir], name);

    return;
}

/******************************************************************************
 *      ASIO Start () SUPPORT FUNCTION: zero_audio_buffer ()
 */
static void
zero_audio_buffer (IWineASIOImpl *This)
{
    int i;
    int frames;

    frames = (This->wineasio_num_ports[WINEASIO_IN]
           +  This->wineasio_num_ports[WINEASIO_OUT])
           * BUF_A_B
           * This->asio_bufsz;

    for (i = 0; i < frames; i++)
    {
        This->jack_audio_buf[i] = 0;
    }

    return;
}

/******************************************************************************
 *      ASIO INTERFACE METHOD: Start ()
 */
DEFINE_THISCALL_WRAPPER (Start,4)
HIDDEN ASIOError STDMETHODCALLTYPE
Start (LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ASIOError        result;
    int              i;

    TRACE ("iface: %p\n", iface);

    ENSURE_DRIVER_STATE (Prepared, "Prepared");

    zero_audio_buffer (This);

    do_buffer_switch (This, 0); // FIXME: remove pos param by folding into dbs()
    This->asio_buf_ndx = This->asio_buf_ndx == BUF_B ? BUF_A : BUF_B;

    do_buffer_switch (This, This->asio_bufsz);
    This->asio_buf_ndx = This->asio_buf_ndx == BUF_B ? BUF_A : BUF_B;

    if (jack_activate (This->jack_client))
    {
        ERR ("Unable to activate JACK client\n");
        result = ASE_NotPresent;
        goto out;
    }

    register_ports (This, WINEASIO_IN);
    register_ports (This, WINEASIO_OUT);

    // FIXME: deal with this

    /* connect to the hardware io */
    if (This->wineasio_conn_ports)
    {
        for (i = 0; i < This->jack_num_phys_ports[WINEASIO_IN]
                 && i < This->wineasio_num_ports[WINEASIO_IN]; i++)
        {
            /* TRACE("Connecting JACK port: %s to asio: %s\n", This->jack_phys_port[WINEASIO_IN][i], jack_port_name(This->wineasio_iochan[WINEASIO_IN][i].port)); */
            if (strstr (jack_port_type (jack_port_by_name (This->jack_client,
                                                           This->jack_phys_port[WINEASIO_IN][i])),
                                                           "audio"))
                if (jack_connect (This->jack_client,
                                  This->jack_phys_port[WINEASIO_IN][i],
                                  jack_port_name (This->wineasio_iochan[WINEASIO_IN][i].port)))
                    WARN ("Unable to connect %s to %s\n",
                          This->jack_phys_port[WINEASIO_IN][i],
                          jack_port_name (This->wineasio_iochan[WINEASIO_IN][i].port));
        }
        for (i = 0; i < This->jack_num_phys_ports[WINEASIO_OUT]
                 && i < This->wineasio_num_ports[WINEASIO_OUT]; i++)
        {
            /* TRACE("Connecting asio: %s to jack port: %s\n", jack_port_name(This->wineasio_iochan[WINEASIO_OUT][i].port), This->jack_phys_port[WINEASIO_OUT][i]); */
            if (strstr (jack_port_type (jack_port_by_name (This->jack_client,
                                                           This->jack_phys_port[WINEASIO_OUT][i])),
                                                           "audio"))
            {
                if (jack_connect (This->jack_client,
                                  jack_port_name (This->wineasio_iochan[WINEASIO_OUT][i].port),
                                  This->jack_phys_port[WINEASIO_OUT][i]))
                {
                    WARN ("Unable to connect to %s\n",
                          jack_port_name (This->wineasio_iochan[WINEASIO_OUT][i].port));
                }
            }
        }
    }

    SET_NEW_STATE_TRACE (Running, "Running");

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: Stop ()
 */
DEFINE_THISCALL_WRAPPER (Stop,4)
HIDDEN ASIOError STDMETHODCALLTYPE
Stop (LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ASIOError        result;

    TRACE ("iface: %p\n", iface);

    ENSURE_DRIVER_STATE (Running, "Running");

    sleep (1);

    /*
     * In jack_deactive () below all the ports belonging to our client will be
     * disconnected.  But we don't clear their record in the port connection
     * database since we will need this info to bring the connections back
     * up during the next Start ().
     */
    This->jack_ignore_port_connect = TRUE;

    TRACE ("about to call jack_deactivate ()\n");

    if (jack_deactivate (This->jack_client))
    {
        ERR ("Unable to deactivate JACK client\n");
        result = ASE_NotPresent;
        goto out;
    }

    TRACE ("returned from jack_deactivate ()\n");

    This->jack_ignore_port_connect = FALSE;

    // FIXME: do we need to dealloc here?

    SET_NEW_STATE_TRACE (Prepared, "Prepared");

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: GetChannels ()
 */
DEFINE_THISCALL_WRAPPER (GetChannels,12)
HIDDEN ASIOError STDMETHODCALLTYPE
GetChannels (LPWINEASIO  iface,
             LONG       *numInpChans,
             LONG       *numOutChans)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ASIOError        result;

    if (!numInpChans || !numOutChans)
    {
        ERR ("Nullpointer argument\n");
        result = ASE_InvalidParameter;
        goto out;
    }

    *numInpChans = This->wineasio_num_ports[WINEASIO_IN];
    *numOutChans = This->wineasio_num_ports[WINEASIO_OUT];

    TRACE ("iface: %p, inputs: %i, outputs: %i\n", iface,
                                                   This->wineasio_num_ports[WINEASIO_IN],
                                                   This->wineasio_num_ports[WINEASIO_OUT]);

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: GetLatencies ()
 */
DEFINE_THISCALL_WRAPPER (GetLatencies,12)
HIDDEN ASIOError STDMETHODCALLTYPE
GetLatencies (LPWINEASIO  iface,
              LONG       *inputLatency,
              LONG       *outputLatency)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ASIOError        result;

    if (!inputLatency || !outputLatency)
    {
        ERR ("Nullpointer argument\n");
        result = ASE_InvalidParameter;
        goto out;
    }

    *inputLatency = *outputLatency = This->asio_bufsz;

    TRACE ("iface: %p, Latency (in and out): %i frames\n",
           iface, This->asio_bufsz);

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: GetBufferSize ()
 */
DEFINE_THISCALL_WRAPPER (GetBufferSize,20)
HIDDEN ASIOError STDMETHODCALLTYPE
GetBufferSize (LPWINEASIO  iface,
               LONG       *minSize,
               LONG       *maxSize,
               LONG       *preferredSize,
               LONG       *granularity)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ASIOError        result;

    TRACE ("iface: %p, minSize: %p, maxSize: %p, preferredSize: %p, granularity: %p\n", iface, minSize, maxSize, preferredSize, granularity);

    if (!minSize || !maxSize || !preferredSize || !granularity)
    {
        ERR ("Nullpointer argument\n");
        result = ASE_InvalidParameter;
        goto out;
    }

    // FIXME clean up this mess

    if (This->wineasio_fixed_bufsz)
    {
        *minSize = *maxSize = *preferredSize = This->asio_bufsz;
        *granularity = -1;
        TRACE ("Buffersize fixed at %i\n", This->asio_bufsz);
        result = ASE_OK;
        goto out;
    }

    *minSize = WINEASIO_MIN_BUFSZ;
    *maxSize = WINEASIO_MAX_BUFSZ;
    *preferredSize = This->wineasio_preferd_bufsz;
    *granularity = -1;
    TRACE ("The ASIO host can control buffersize; "
           "min: %i, max: %i, pref: %i, gran: %i, curr: %i\n",
           *minSize,
           *maxSize,
           *preferredSize,
           *granularity,
           This->asio_bufsz);

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: CanSampleRate ()
 */
DEFINE_THISCALL_WRAPPER (CanSampleRate,12)
HIDDEN ASIOError STDMETHODCALLTYPE
CanSampleRate (LPWINEASIO     iface,
               ASIOSampleRate sampleRate)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ASIOError        result;

    TRACE ("iface: %p, rate: %li, req: %li\n",
           iface, (long) This->asio_smpl_rate, (long) sampleRate);

    if (sampleRate != This->asio_smpl_rate)
    {
        result = ASE_NoClock;
        goto out;
    }

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: GetSampleRate ()
 */
DEFINE_THISCALL_WRAPPER(GetSampleRate,8)
HIDDEN ASIOError STDMETHODCALLTYPE
GetSampleRate(LPWINEASIO      iface,
              ASIOSampleRate *sampleRate)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ASIOError        result;

    TRACE ("iface: %p, rate: %i\n", iface, (int) This->asio_smpl_rate);

    if (!sampleRate)
    {
        ERR ("Nullpointer argument\n");
        result = ASE_InvalidParameter;
        goto out;
    }

    *sampleRate = This->asio_smpl_rate;

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: SetSampleRate ()
 */
DEFINE_THISCALL_WRAPPER (SetSampleRate,12)
HIDDEN ASIOError STDMETHODCALLTYPE
SetSampleRate (LPWINEASIO     iface,
               ASIOSampleRate sampleRate)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ASIOError        result;

    TRACE ("iface: %p, req: %f\n", iface, sampleRate);

    if (sampleRate != This->asio_smpl_rate)
    {
        result = ASE_NoClock;
        goto out;
    }

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: GetClockSources ()
 */
DEFINE_THISCALL_WRAPPER (GetClockSources,12)
HIDDEN ASIOError STDMETHODCALLTYPE
GetClockSources (LPWINEASIO       iface,
                 ASIOClockSource *clocks,
                 LONG            *numSources)
{
    ASIOError   result;

    TRACE ("iface: %p, clocks: %p, numSources: %p\n", iface, clocks, numSources);

    if (!clocks || !numSources)
    {
        ERR ("Nullpointer argument\n");
        result = ASE_InvalidParameter;
        goto out;
    }

    // FIXME cleanup

    clocks->index = 0;
    clocks->associatedChannel = -1;
    clocks->associatedGroup = -1;
    clocks->isCurrentSource = ASIOTrue;
    strcpy (clocks->name, "Internal");
    *numSources = 1;

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: SetClockSource ()
 */
DEFINE_THISCALL_WRAPPER (SetClockSource,8)
HIDDEN ASIOError STDMETHODCALLTYPE
SetClockSource (LPWINEASIO iface,
                LONG       index)
{
    ASIOError   result;

    TRACE ("iface: %p, index: %i\n", iface, index);

    if (index != 0)
    {
        result = ASE_NotPresent;
        goto out;
    }

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: GetSamplePosition ()
 */
DEFINE_THISCALL_WRAPPER (GetSamplePosition,12)
HIDDEN ASIOError STDMETHODCALLTYPE
GetSamplePosition (LPWINEASIO     iface,
                   ASIOSamples   *sPos,
                   ASIOTimeStamp *tStamp)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ASIOError        result;

    TRACE ("iface: %p, sPos: %p, tStamp: %p\n", iface, sPos, tStamp);

    if (!sPos || !tStamp)
    {
        WARN ("Nullpointer argument\n");
        result = ASE_InvalidParameter;
        goto out;
    }

    // FIXME
#ifdef _WIN64
    *tStamp = This->asio_tstmp;
    *sPos = This->asio_smpl_pos;
#else
    tStamp->lo = This->asio_tstmp.lo;
    tStamp->hi = This->asio_tstmp.hi;
    sPos->lo = This->asio_smpl_pos.lo;
    sPos->hi = 0;  // FIXME
#endif

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: GetChannelInfo ()
 */
DEFINE_THISCALL_WRAPPER (GetChannelInfo,8)
HIDDEN ASIOError STDMETHODCALLTYPE
GetChannelInfo (LPWINEASIO       iface,
                ASIOChannelInfo *info)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ASIOError        result;

    TRACE ("iface: %p, info: %p\n", iface, info);

    if (info->channel < 0 || (info->isInput
                                    ? info->channel >= This->wineasio_num_ports[WINEASIO_IN]
                                    : info->channel >= This->wineasio_num_ports[WINEASIO_OUT]))
    {
        ERR ("Invalid Parameter\n");
        result = ASE_InvalidParameter;
        goto out;
    }

    // FIXME

    info->channelGroup = 0;
    info->type = WINEASIO_SMPL_FMT;

    if (info->isInput)
    {
        info->isActive = This->wineasio_iochan[WINEASIO_IN][info->channel].active;
        memcpy (info->name, This->wineasio_iochan[WINEASIO_IN][info->channel].port_name,
                WINEASIO_MAX_NAME_LEN);     // FIXME: strcpy?
    }
    else
    {
        info->isActive = This->wineasio_iochan[WINEASIO_OUT][info->channel].active;
        memcpy (info->name, This->wineasio_iochan[WINEASIO_OUT][info->channel].port_name,
                WINEASIO_MAX_NAME_LEN);
    }

    TRACE ("chan: %d, isInp: %d, isAct: %d, chanGrp: %d, typ: %d, nam: %s\n",
           info->channel, info->isInput, info->isActive, info->channelGroup,
           info->type, info->name);

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO CreateBuffers () SUPPORT FUNCTIONS: Forward Declarations
 */
// FIXME

/******************************************************************************
 *      ASIO CreateBuffers () SUPPORT FUNCTION: map_active_buf_halves ()
 */
static void
map_active_chans (IWineASIOImpl  *This,
                  ASIOBufferInfo *buf_info,
                  LONG            numChans)
{
    int i;

    This->asio_active[WINEASIO_IN] = This->asio_active[WINEASIO_OUT] = 0;

    for (i = 0; i < numChans; i++, buf_info++)
    {
        if (buf_info->isInput)
        {
            buf_info->buffers[BUF_A] // FIXME: the index on 'inp' starts at 0, but the index of the channel might be 2!
                = &This->wineasio_iochan[WINEASIO_IN][This->asio_active[WINEASIO_IN]]
                    .audio_buf[BUF_A];
            buf_info->buffers[BUF_B]
                = &This->wineasio_iochan[WINEASIO_IN][This->asio_active[WINEASIO_IN]]
                    .audio_buf[This->asio_bufsz];
            This->wineasio_iochan[WINEASIO_IN][This->asio_active[WINEASIO_IN]].active = TRUE;
            This->asio_active[WINEASIO_IN]++;
            TRACE ("isInput; channelNum=%d\n", buf_info->channelNum);
        }
        else
        {
            buf_info->buffers[BUF_A]
                = &This->wineasio_iochan[WINEASIO_OUT][This->asio_active[WINEASIO_OUT]]
                    .audio_buf[BUF_A];
            buf_info->buffers[BUF_B]
                = &This->wineasio_iochan[WINEASIO_OUT][This->asio_active[WINEASIO_OUT]]
                    .audio_buf[This->asio_bufsz];
            This->wineasio_iochan[WINEASIO_OUT][This->asio_active[WINEASIO_OUT]].active = TRUE;
            This->asio_active[WINEASIO_OUT]++;
            TRACE ("isOutput; channelNum=%d\n", buf_info->channelNum);
        }
    }

    TRACE ("%i act in; %i act out; audio channels inited\n",
           This->asio_active[WINEASIO_IN], This->asio_active[WINEASIO_OUT]);

    return;
}

/******************************************************************************
 *      ASIO CreateBuffers () SUPPORT FUNCTION: map_buf_to_channels ()
 */
static void
map_chans_to_buf (IWineASIOImpl *This)
{
    int     i;
    LONG    frames;

    frames = This->asio_bufsz * BUF_A_B;

    for (i = 0; i < This->wineasio_num_ports[WINEASIO_IN]; i++)
    {
        This->wineasio_iochan[WINEASIO_IN][i].audio_buf = This->jack_audio_buf
                                                        + i * frames;
    }

    for (i = 0; i < This->wineasio_num_ports[WINEASIO_OUT]; i++)
    {
        This->wineasio_iochan[WINEASIO_OUT][i].audio_buf = This->jack_audio_buf
            + (This->wineasio_num_ports[WINEASIO_IN] + i) * frames;
    }

    return;
}

/******************************************************************************
 *      ASIO CreateBuffers () SUPPORT FUNCTION: allocate_audio_buffers ()
 */
static ASIOError
alloc_audio_buf (IWineASIOImpl  *This,
                 ASIOBufferInfo *bufInfo,
                 LONG            numChans)
{
    int              total_ports;
    LONG             dbl_buf_frames;
    LONG             dbl_buf_bytes;
    LONG             audio_buf_sz;
    ASIOError        result;

    total_ports = This->wineasio_num_ports[WINEASIO_IN] + This->wineasio_num_ports[WINEASIO_OUT];

    dbl_buf_frames  = BUF_A_B * This->asio_bufsz;
    dbl_buf_bytes   = dbl_buf_frames * sizeof (jack_default_audio_sample_t);

    audio_buf_sz = total_ports * dbl_buf_bytes;  // FIXME s/total_ports/numChans/?

    /*
     * Allocate a space large enough to accomodate the case
     * where all channels are active.
     */
    This->jack_audio_buf = HeapAlloc (GetProcessHeap (), 0, audio_buf_sz);
    if (!This->jack_audio_buf)
    {
        ERR ("Unable to allocate %i ASIO audio buffers\n", total_ports);
        result = ASE_NoMemory;
        goto out;
    }

    TRACE ("%i ASIO audio buffers allocated (%i kB)\n", total_ports,
           (int) (audio_buf_sz / KB));

    result = ASE_OK;

out:
    return result;
}

static ASIOBool
update_bufInfo_bufs (IWineASIOImpl  *This,
                     ASIOBufferInfo *bufInfo,
                     LONG            numChans)
{
    ASIOBool    result;

    result = alloc_audio_buf (This, bufInfo, numChans);
    if (result != ASE_OK)
    {
        goto out;
    }

    map_chans_to_buf (This);

    map_active_chans (This, bufInfo, numChans);

out:
    return result;
}

/******************************************************************************
 *      ASIO CreateBuffers () SUPPORT FUNCTION: query_msg_selectors ()
 */
typedef struct {
    const char *str;
    BOOL  isQuery;
}
sel;

static sel selector[] =
{
    {
        "(undefined)",              FALSE
    },
    {
        "AsioSelectorSupported",    FALSE   /* We can't query whether the *
                                              * query works, so FALSE. ;-) */
    },
    {
        "AsioEngineVersion",        TRUE
    },
    {
        "AsioResetRequest",         FALSE
    },
    {
        "AsioBufferSizeChange",     FALSE
    },
    {
        "AsioResyncRequest",        FALSE
    },
    {
        "AsioLatenciesChanged",     FALSE
    },
    {
        "AsioSupportsTimeInfo",     TRUE
    },
    {
        "AsioSupportsTimeCode",     TRUE
    },
    {
        "AsioSupportsInputMonitor", TRUE
    },
    {
        "AsioSupportsInputGain",    TRUE
    },
    {
        "AsioSupportsInputMeter",   TRUE
    },
    {
        "AsioOverload",             FALSE
    },
    {
        NULL
    }
};

static void
query_msg_selectors (IWineASIOImpl *This)
{
    LONG        i;

    // FIXME start at 2.  we terminate on a NULL in case the list of selectors
    // is expanded in a supsequent release.
    // FIXME: start at 1?

    for (i = 2; selector[i].str; i++)
    {
        if (This->asio_cbs->asioMessage (kAsioSelectorSupported, i, 0, 0))
        {
            This->wineasio_sel_msg[i].is_sel_support = TRUE;
            TRACE ("Host groks `%s' selector\n", selector[i].str);

            if (selector[i].isQuery)
            {
                This->wineasio_sel_msg[i].query_rslt
                    = This->asio_cbs->asioMessage (i, 0, 0, 0);

                TRACE ("Host's answer to `%s' query was (%d)\n",
                       selector[i].str, This->wineasio_sel_msg[i].query_rslt);
            }
            else
            {
                This->wineasio_sel_msg[i].query_rslt = NO_QUERY_RESULT;
            }
        }
        else
        {
            This->wineasio_sel_msg[i].is_sel_support = FALSE;
            TRACE ("Host doesn't support `%s' selector\n", selector[i].str);
        }
    }

    return;
}

/******************************************************************************
 *      ASIO CreateBuffers () SUPPORT FUNCTION: validate_bufsz ()
 */
static ASIOError
validate_bufsz (IWineASIOImpl *This,
                LONG           bufSz)
{
    ASIOError   result;

    if (This->wineasio_fixed_bufsz) // FIXME: is this set somewhere?
    {
        if (This->asio_bufsz != bufSz)
        {
            ERR ("Buffersize is fixed at (%d) and (%d) requested; failing\n",
                  This->asio_bufsz, bufSz);
            result = ASE_InvalidMode;
            goto out;
        }
        TRACE ("Buffersize fixed at (%d) and (%d) requested; all good\n",
               This->asio_bufsz, bufSz);
    }
    else
    {
        if (!(bufSz > 0
                && !(bufSz & (bufSz - 1))
                && bufSz >= WINEASIO_MIN_BUFSZ
                && bufSz <= WINEASIO_MAX_BUFSZ))
        {
            ERR ("Invalid buffersize (%i) requested (not a power of 2); failing\n",
                 bufSz);
            result = ASE_InvalidMode;
            goto out;
        }

        if (This->asio_bufsz == bufSz)
        {
            TRACE ("Req buffersize (%d) == curr buffersize (%d); all good\n",
                   bufSz, This->asio_bufsz);
        }
        else
        {
            if (jack_set_buffer_size (This->jack_client, bufSz))
            {
                ERR ("JACK was unable to set new buffersize of (%d) from (%d);"
                     "failing\n", bufSz, This->asio_bufsz);
                result = ASE_HWMalfunction;
                goto out;
            }

            TRACE ("Buffer size changed from (%d) to (%d); all good\n",
                   This->asio_bufsz, bufSz);

            This->asio_bufsz = bufSz;
        }
    }

    result = ASE_OK;

out:
    return result;
}

/******************************************************************************
 *      ASIO CreateBuffers () SUPPORT FUNCTION: validate_num_chans ()
 */
static ASIOError
validate_num_chans (IWineASIOImpl     *This,
                       LONG            num_chans,
                       ASIOBufferInfo *buf_info)
{
    int         i;
    int         j;
    int         k;
    ASIOError   result;

    // FIXME: parameterize this

    for (i = j = k = 0; i < num_chans; i++, buf_info++)
    {
        if (buf_info->isInput)
        {
            if (j++ >= This->wineasio_num_ports[WINEASIO_IN])
            {
                ERR ("Invalid input channel requested\n");
                result = ASE_InvalidMode;
                goto out;
            }
        }
        else
        {
            if (k++ >= This->wineasio_num_ports[WINEASIO_OUT])
            {
                ERR ("Invalid output channel requested\n");
                result = ASE_InvalidMode;
                goto out;
            }
        }
    }

    result = ASE_OK;

out:
    return result;
}

/******************************************************************************
 *      ASIO INTERFACE METHOD: CreateBuffers ()
 */
DEFINE_THISCALL_WRAPPER (CreateBuffers,20)
HIDDEN ASIOError STDMETHODCALLTYPE
CreateBuffers (LPWINEASIO      iface,
               ASIOBufferInfo *bufInfo,
               LONG            numChans,
               LONG            bufSz,
               ASIOCallbacks  *asioCbs)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    ASIOError        result;

    TRACE ("iface: %p, bufInfo: %p, numChans: %i, bufSz: %i, asioCbs: %p\n",
           iface, bufInfo, numChans, bufSz, asioCbs);

    /*
     * Check if we're in the right state for this function.
     */
    ENSURE_DRIVER_STATE (Initialized, "Initialized");

    /*
     * Unlikely that we were passed a NULL callbacks ptr,
     * but check just in case.  Save the asioCallbacks ptr.
     */
    if (!asioCbs)
    {
        ERR ("The host didn't provide a callbacks structure pointer!\n");
        result = ASE_NotPresent;
        goto out;
    }
    This->asio_cbs = asioCbs;

    /*
     * Ensure that `numChans' does not exceed the individual
     * `num_ports_[inp|out]' for both types of channel.
     */
    result = validate_num_chans (This, numChans, bufInfo);
    if (result != ASE_OK)
    {
        goto out;
    }

    /*
     * Validate the buffersize we've been passed and set it as the current
     * buffersize if not fixed.
     */
    result = validate_bufsz (This, bufSz);
    if (result != ASE_OK)
    {
        goto out;
    }

    /*
     * Find out which `asioMessage()' selectors are understood by the host.
     * Of these selectors, if the selector is a query, query the host for
     * that selector's result.  This will indicate which host features are
     * available to WineASIO.  Selectors which aren't queries are marked as
     * either understood or not understood for future driver use.
     */
    query_msg_selectors(This);

    result = update_bufInfo_bufs (This, bufInfo, numChans);
    if (result != ASE_OK)
    {
        goto out;
    }

    SET_NEW_STATE_TRACE (Prepared, "Prepared");

    result = ASE_OK;

out:
    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: DisposeBuffers ()
 */
DEFINE_THISCALL_WRAPPER (DisposeBuffers,4)
HIDDEN ASIOError STDMETHODCALLTYPE
DisposeBuffers (LPWINEASIO iface)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;
    int              i;
    ASIOError        result;

    TRACE("iface: %p\n", iface);

    if (This->wineasio_drv_state == Running)
    {
        Stop (iface);
    }

    ENSURE_DRIVER_STATE (Prepared, "Prepared");

    This->asio_cbs = NULL;

    // FIXME

    for (i = 0; i < This->wineasio_num_ports[WINEASIO_IN]; i++)
    {
        This->wineasio_iochan[WINEASIO_IN][i].audio_buf = NULL;
        This->wineasio_iochan[WINEASIO_IN][i].active = FALSE;
    }

    for (i = 0; i < This->wineasio_num_ports[WINEASIO_OUT]; i++)
    {
        This->wineasio_iochan[WINEASIO_OUT][i].audio_buf = NULL;
        This->wineasio_iochan[WINEASIO_OUT][i].active = FALSE;
    }

    This->asio_active[WINEASIO_IN] = This->asio_active[WINEASIO_OUT] = 0;

    if (This->jack_audio_buf)
    {
        HeapFree (GetProcessHeap (), 0, This->jack_audio_buf);
    }

    SET_NEW_STATE_TRACE (Initialized, "Initialized");

    result = ASE_OK;

    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: ControlPanel ()
 */
DEFINE_THISCALL_WRAPPER (ControlPanel,4)
HIDDEN ASIOError STDMETHODCALLTYPE
ControlPanel (LPWINEASIO iface)
{
    IWineASIOImpl  *This = (IWineASIOImpl *) iface;
    char           *arg_list[] = { strdup ("qjackctl"), NULL };
    double          delay;
    ASIOError       result;

    TRACE ("iface: %p\n", iface);

    delay = (double)jack_get_max_delayed_usecs (This->jack_client); // FIXME
    TRACE ("delay: %f\n", delay);

    if (!fork ())
    {
        execvp (arg_list[0], arg_list);
    }

    result = ASE_OK;

    FUNC_COMPLETION_ASEOK_TRACE (result);
    return result;
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: Future ()
 */
DEFINE_THISCALL_WRAPPER (Future,12)
HIDDEN ASIOError STDMETHODCALLTYPE
Future (LPWINEASIO  iface,
        LONG        selector,
        void       *opt)
{
    IWineASIOImpl   *This = (IWineASIOImpl *) iface;

    TRACE ("iface: %p, selector: %i, opt: %p\n", iface, selector, opt);

    switch (selector)
    {
        case kAsioEnableTimeCodeRead:
            This->asio_tcode_enabled = TRUE;
            TRACE ("The ASIO host enabled TimeCode\n");
            return ASE_SUCCESS;
        case kAsioDisableTimeCodeRead:
            This->asio_tcode_enabled = FALSE;
            TRACE ("The ASIO host disabled TimeCode\n");
            return ASE_SUCCESS;
        case kAsioSetInputMonitor:
            TRACE ("The driver denied request to set input monitor\n");
            return ASE_NotPresent;
        case kAsioTransport:
            TRACE ("The driver denied request for ASIO Transport control\n");
            return ASE_InvalidParameter;
        case kAsioSetInputGain:
            TRACE ("The driver denied request to set input gain\n");
            return ASE_InvalidParameter;
        case kAsioGetInputMeter:
            TRACE ("The driver denied request to get input meter \n");
            return ASE_InvalidParameter;
        case kAsioSetOutputGain:
            TRACE ("The driver denied request to set output gain\n");
            return ASE_InvalidParameter;
        case kAsioGetOutputMeter:
            TRACE ("The driver denied request to get output meter\n");
            return ASE_InvalidParameter;
        case kAsioCanInputMonitor:
            TRACE ("The driver does not support input monitor\n");
            return ASE_InvalidParameter;
        case kAsioCanTimeInfo:
            TRACE ("The driver supports TimeInfo\n");
            return ASE_SUCCESS;
        case kAsioCanTimeCode:
            TRACE ("The driver supports TimeCode\n");
            return ASE_SUCCESS;
        case kAsioCanTransport:
            TRACE ("The driver denied request for ASIO Transport\n");
            return ASE_InvalidParameter;
        case kAsioCanInputGain:
            TRACE ("The driver does not support input gain\n");
            return ASE_InvalidParameter;
        case kAsioCanInputMeter:
            TRACE ("The driver does not support input meter\n");
            return ASE_InvalidParameter;
        case kAsioCanOutputGain:
            TRACE ("The driver does not support output gain\n");
            return ASE_InvalidParameter;
        case kAsioCanOutputMeter:
            TRACE ("The driver does not support output meter\n");
            return ASE_InvalidParameter;
        case kAsioSetIoFormat:
            TRACE ("The driver denied request to set DSD IO format\n");
            return ASE_NotPresent;
        case kAsioGetIoFormat:
            TRACE ("The driver denied request to get DSD IO format\n");
            return ASE_NotPresent;
        case kAsioCanDoIoFormat:
            TRACE ("The driver does not support DSD IO format\n");
            return ASE_NotPresent;
        default:
            TRACE ("ASIOFuture() called with undocumented selector\n");
            return ASE_InvalidParameter;
    }
}


/******************************************************************************
 *      ASIO INTERFACE METHOD: OutputReady ()
 */
DEFINE_THISCALL_WRAPPER (OutputReady,4)
HIDDEN ASIOError STDMETHODCALLTYPE
OutputReady (LPWINEASIO iface)
{
    /* disabled to stop stand alone NI programs from spamming the console */
//    TRACE ("iface: %p\n", iface);  // FIXME create tweakable #define

    return ASE_NotPresent;
}


/******************************************************************************
 *      Vtbl
 */
static const IWineASIOVtbl WineASIO_Vtbl =
{
    (void *) QueryInterface,
    (void *) AddRef,
    (void *) Release,

    (void *) THISCALL (Init),
    (void *) THISCALL (GetDriverName),
    (void *) THISCALL (GetDriverVersion),
    (void *) THISCALL (GetErrorMessage),
    (void *) THISCALL (Start),
    (void *) THISCALL (Stop),
    (void *) THISCALL (GetChannels),
    (void *) THISCALL (GetLatencies),
    (void *) THISCALL (GetBufferSize),
    (void *) THISCALL (CanSampleRate),
    (void *) THISCALL (GetSampleRate),
    (void *) THISCALL (SetSampleRate),
    (void *) THISCALL (GetClockSources),
    (void *) THISCALL (SetClockSource),
    (void *) THISCALL (GetSamplePosition),
    (void *) THISCALL (GetChannelInfo),
    (void *) THISCALL (CreateBuffers),
    (void *) THISCALL (DisposeBuffers),
    (void *) THISCALL (ControlPanel),
    (void *) THISCALL (Future),
    (void *) THISCALL (OutputReady)
};


/******************************************************************************
 *      COM ENTRYPOINT: WineASIOCreateInstance ()
 *
 * Allocate the interface pointer and associate
 * it with the vtbl/WineASIO object */
HRESULT WINAPI
WineASIOCreateInstance (REFIID  riid,
                        LPVOID *ppobj)
{
    IWineASIOImpl   *pobj;
    LONG             result;

    TRACE ("riid: %s, ppobj: %p\n", debugstr_guid(riid), ppobj);

    pobj = HeapAlloc (GetProcessHeap (), 0, sizeof (*pobj));
    if (pobj == NULL)
    {
        ERR ("out of memory\n");
        result = E_OUTOFMEMORY;
        goto out;
    }

    pobj->lpVtbl = &WineASIO_Vtbl;
    pobj->ref    = 1;
    *ppobj       = pobj;

    result = S_OK;

    TRACE ("completed successfully (S_OK)\n");

out:
    return result;
}
