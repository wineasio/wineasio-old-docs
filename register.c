/*
 * Self-registerable DLL functions for wineasio.dll
 *
 * Copyright (C) 2003 John K. Hohm
 * Copyright (C) 2006 Robert Reif
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

/*
 * Near the bottom of this file are the exported DllRegisterServer and
 * DllUnregisterServer, which make all this worthwhile.
 */

#include <stdarg.h>

#define NONAMELESSSTRUCT
#define NONAMELESSUNION

#include <windows.h>
#include <advpub.h>
#include <objbase.h>
#include <wine/debug.h>

#include "classid.h"


WINE_DEFAULT_DEBUG_CHANNEL(asio);


/******************************************************************************
 *      Class and interface structure declarations
 */

struct regsvr_coclass
{
    CLSID   const *clsid;         /* NULL for end of list */
    LPCSTR         name;          /* can be NULL to omit */
    LPCSTR         ips;           /* can be NULL to omit */
    LPCSTR         ips32;         /* can be NULL to omit */
    LPCSTR         ips32_tmodel;  /* can be NULL to omit */
    LPCSTR         progid;        /* can be NULL to omit */
    LPCSTR         viprogid;      /* can be NULL to omit */
    LPCSTR         progid_extra;  /* can be NULL to omit */
};

struct regsvr_interface
{
    IID     const *iid;           /* NULL for end of list */
    LPCSTR         name;          /* can be NULL to omit */
    IID     const *base_iid;      /* can be NULL to omit */
    int            num_methods;   /* can be <0 to omit */
    CLSID   const *ps_clsid;      /* can be NULL to omit */
    CLSID   const *ps_clsid32;    /* can be NULL to omit */
};


/******************************************************************************
 *      Static function declarations
 */

static void create_failed (LONG rc);
static void set_failed (LONG rc);

static LONG recursive_delete_key  (HKEY         key);
static LONG recursive_delete_keyA (HKEY         base,
                                   char  const *name);
static LONG recursive_delete_keyW (HKEY         base,
                                   WCHAR const *name);

static HRESULT unregister_driver     (void);
static HRESULT unregister_interfaces (struct regsvr_interface const *list);
static HRESULT unregister_coclasses  (struct regsvr_coclass   const *list);

static LONG register_key_defvalueA (HKEY         base,
                                    WCHAR const *name,
                                    char  const *value);
static LONG register_key_defvalueW (HKEY         base,
                                    WCHAR const *name,
                                    WCHAR const *value);
static LONG register_key_guid      (HKEY         base,
                                    WCHAR const *name,
                                    GUID  const *guid);

static LONG register_progid (WCHAR const *clsid,
                             char  const *progid,
                             char  const *curver_progid,
                             char  const *name,
                             char  const *extra);

static HRESULT register_driver     (void);
static HRESULT register_interfaces (struct regsvr_interface const *list);
static HRESULT register_coclasses  (struct regsvr_coclass   const *list);


/******************************************************************************
 *      String constant definitions
 */
static WCHAR const clsid_keyname[] =
{
    'C', 'L', 'S', 'I', 'D', 0
};
static WCHAR const ips_keyname[] =
{
    'I', 'n', 'P', 'r', 'o', 'c', 'S', 'e', 'r', 'v', 'e', 'r', 0
};
static WCHAR const ips32_keyname[] =
{
    'I', 'n', 'P', 'r', 'o', 'c', 'S', 'e', 'r', 'v', 'e', 'r', '3', '2', 0
};
static WCHAR const progid_keyname[] =
{
    'P', 'r', 'o', 'g', 'I', 'D', 0
};
static WCHAR const interface_keyname[] =
{
    'I', 'n', 't', 'e', 'r', 'f', 'a', 'c', 'e', 0
};
static WCHAR const base_ifa_keyname[] =
{
    'B', 'a', 's', 'e', 'I', 'n', 't', 'e', 'r', 'f', 'a', 'c', 'e', 0
};
static WCHAR const num_methods_keyname[] =
{
    'N', 'u', 'm', 'M', 'e', 't', 'h', 'o', 'd', 's', 0
};
static WCHAR const ps_clsid_keyname[] =
{
    'P', 'r', 'o', 'x', 'y', 'S', 't', 'u', 'b', 'C', 'l', 's', 'i', 'd', 0
};
static WCHAR const ps_clsid32_keyname[] =
{
    'P', 'r', 'o', 'x', 'y', 'S', 't', 'u', 'b', 'C', 'l', 's', 'i', 'd', '3',
    '2', 0
};
static WCHAR const curver_keyname[] =
{
    'C', 'u', 'r', 'V', 'e', 'r', 0
};
static WCHAR const viprogid_keyname[] =
{
    'V', 'e', 'r', 's', 'i', 'o', 'n', 'I', 'n', 'd', 'e', 'p', 'e', 'n', 'd',
    'e', 'n', 't', 'P', 'r', 'o', 'g', 'I', 'D', 0
};

static char const tmodel_valuename[] = "ThreadingModel";


/******************************************************************************
 *      Static Helper Functions
 */

/******************************************************************************
 *      create_failed
 */
static void
create_failed (LONG rc)
{
    ERR ("RegCreateKeyEx failed with code %d\n", rc);
    return;
}

/******************************************************************************
 *      set_failed
 */
static void
set_failed (LONG rc)
{
    ERR ("RegSetValueEx failed with code %d\n", rc);
    return;
}

/******************************************************************************
 *      recursive_delete_key
 */
static LONG
recursive_delete_key (HKEY key)
{
    LONG    res;
    WCHAR   subkey_name[MAX_PATH];
    DWORD   cName;
    HKEY    subkey;

    for (;;)
    {
        cName = sizeof (subkey_name) / sizeof (WCHAR);
        res = RegEnumKeyExW (key, 0,
                             subkey_name, &cName, NULL,
                             NULL, NULL,
                             NULL);
        if (res != ERROR_SUCCESS && res != ERROR_MORE_DATA)
        {
            res = ERROR_SUCCESS; /* presumably we're done enumerating */
            break;
        }

        res = RegOpenKeyExW (key, subkey_name, 0,
                             KEY_READ | KEY_WRITE,
                             &subkey);
        if (res == ERROR_FILE_NOT_FOUND)
        {
            continue;
        }
        if (res != ERROR_SUCCESS)
        {
            break;
        }

        res = recursive_delete_key (subkey);

        RegCloseKey (subkey);

        if (res != ERROR_SUCCESS)
        {
            break;
        }
    }

    if (res == ERROR_SUCCESS)
    {
        res = RegDeleteKeyW (key, 0);
    }

    return res;
}

/******************************************************************************
 *      recursive_delete_keyA
 */
static LONG
recursive_delete_keyA (HKEY        base,
                       char const *name)
{
    LONG    res;
    HKEY    key;

    res = RegOpenKeyExA (base, name, 0,
                         KEY_READ | KEY_WRITE,
                         &key);
    if (res == ERROR_FILE_NOT_FOUND)
    {
        res = ERROR_SUCCESS;
        goto out;
    }
    if (res != ERROR_SUCCESS)
    {
        goto out;
    }

    res = recursive_delete_key (key);

    RegCloseKey (key);
out:
    return res;
}

/******************************************************************************
 *      recursive_delete_keyW
 */
static LONG
recursive_delete_keyW(HKEY         base,
                      WCHAR const *name)
{
    LONG    res;
    HKEY    key;

    res = RegOpenKeyExW (base, name, 0,
                         KEY_READ | KEY_WRITE,
                         &key);
    if (res == ERROR_FILE_NOT_FOUND)
    {
        res = ERROR_SUCCESS;
        goto out;
    }
    if (res != ERROR_SUCCESS)
    {
        goto out;
    }

    res = recursive_delete_key (key);

    RegCloseKey (key);
out:
    return res;
}

/******************************************************************************
 *      unregister_driver
 */
static HRESULT
unregister_driver (void)
{
    LPCSTR  asio_key = "Software\\ASIO\\WineASIO";

    /* FIXME */
    return recursive_delete_keyA (HKEY_LOCAL_MACHINE, asio_key);
}

/******************************************************************************
 *      unregister_interfaces
 */
static HRESULT
unregister_interfaces (struct regsvr_interface const *list)
{
    LONG    res = ERROR_SUCCESS;
    HKEY    interface_key;

    res = RegOpenKeyExW (HKEY_CLASSES_ROOT, interface_keyname, 0,
                         KEY_READ | KEY_WRITE,
                         &interface_key);
    if (res == ERROR_FILE_NOT_FOUND)
    {
        res = ERROR_SUCCESS;
        goto out;
    }
    if (res != ERROR_SUCCESS)
    {
        goto out;
    }

    for (; res == ERROR_SUCCESS && list->iid; ++list)
    {
        WCHAR buf[MAX_GUID_STRING_LEN];

        StringFromGUID2 (list->iid,
                         buf,
                         MAX_GUID_STRING_LEN);

        res = recursive_delete_keyW (interface_key,
                                     buf);
    }
    RegCloseKey (interface_key);
out:
    return res != ERROR_SUCCESS ? HRESULT_FROM_WIN32(res) : S_OK;
}

/******************************************************************************
 *      unregister_coclasses
 */
static HRESULT
unregister_coclasses (struct regsvr_coclass const *list)
{
    LONG    res = ERROR_SUCCESS;
    HKEY    coclass_key;

    res = RegOpenKeyExW (HKEY_CLASSES_ROOT, clsid_keyname, 0,
                         KEY_READ | KEY_WRITE,
                         &coclass_key);
    if (res == ERROR_FILE_NOT_FOUND)
    {
        res = ERROR_SUCCESS;
        goto out;
    }
    if (res != ERROR_SUCCESS)
    {
        goto out;
    }

    for (; res == ERROR_SUCCESS && list->clsid; ++list)
    {
        WCHAR   buf[MAX_GUID_STRING_LEN];

        StringFromGUID2 (list->clsid,
                         buf,
                         MAX_GUID_STRING_LEN);
        res = recursive_delete_keyW (coclass_key,
                                     buf);
        if (res != ERROR_SUCCESS)
        {
            goto err_out_close_coclass_key;
        }

        if (list->progid)
        {
            res = recursive_delete_keyA (HKEY_CLASSES_ROOT,
                                         list->progid);
            if (res != ERROR_SUCCESS)
            {
                goto err_out_close_coclass_key;
            }
        }

        if (list->viprogid)
        {
            res = recursive_delete_keyA (HKEY_CLASSES_ROOT,
                                         list->viprogid);
            if (res != ERROR_SUCCESS)
            {
                goto err_out_close_coclass_key;
            }
        }
    }
err_out_close_coclass_key:
    RegCloseKey (coclass_key);
out:
    return res != ERROR_SUCCESS ? HRESULT_FROM_WIN32 (res) : S_OK;
}

/******************************************************************************
 *      register_key_defvalueA
 */
static LONG
register_key_defvalueA (HKEY         base,
                        WCHAR const *name,
                        char  const *value)
{
    LONG    res;
    HKEY    key;

    res = RegCreateKeyExW (base, name, 0,
                           NULL,
                           REG_OPTION_NON_VOLATILE,
                           KEY_READ | KEY_WRITE, NULL,
                           &key, NULL);
    if (res != ERROR_SUCCESS)
    {
        create_failed (res);
        goto out;
    }

    res = RegSetValueExA (key, NULL, 0,
                          REG_SZ, (const BYTE *) value,
                          lstrlenA (value) + 1);
    if (res != ERROR_SUCCESS)
    {
        set_failed (res);
    }

    RegCloseKey (key);
out:
    return res;
}

/******************************************************************************
 *      register_key_defvalueW
 */
static LONG
register_key_defvalueW (HKEY         base,
                        WCHAR const *name,
                        WCHAR const *value)
{
    LONG    res;
    HKEY    key;

    res = RegCreateKeyExW (base, name, 0,
                           NULL,
                           REG_OPTION_NON_VOLATILE,
                           KEY_READ | KEY_WRITE, NULL,
                           &key, NULL);
    if (res != ERROR_SUCCESS)
    {
        create_failed (res);
        goto out;
    }

    res = RegSetValueExW (key, NULL, 0,
                          REG_SZ, (const BYTE *) value,
                          (lstrlenW (value) + 1) * sizeof (WCHAR));
    if (res != ERROR_SUCCESS)
    {
        set_failed (res);
    }

    RegCloseKey (key);
out:
    return res;
}

/******************************************************************************
 *      register_key_guid
 */
static LONG
register_key_guid (HKEY         base,
                   WCHAR const *name,
                   GUID  const *guid)
{
    WCHAR   buf[MAX_GUID_STRING_LEN];

    StringFromGUID2 (guid, buf, MAX_GUID_STRING_LEN);

    return register_key_defvalueW (base,
                                   name,
                                   buf);
}

/******************************************************************************
 *      register_progid
 */
static LONG
register_progid (WCHAR const *clsid,
                 char  const *progid,
                 char  const *curver_progid,
                 char  const *name,
                 char  const *extra)
{
    LONG    res;
    HKEY    progid_key;

    res = RegCreateKeyExA (HKEY_CLASSES_ROOT, progid, 0,
                           NULL,
                           REG_OPTION_NON_VOLATILE,
                           KEY_READ | KEY_WRITE, NULL,
                           &progid_key, NULL);
    if (res != ERROR_SUCCESS)
    {
        create_failed (res);
        goto out;
    }

    if (name)
    {
        res = RegSetValueExA (progid_key, NULL, 0,
                              REG_SZ, (const BYTE *) name,
                              strlen (name) + 1);
        if (res != ERROR_SUCCESS)
        {
            set_failed (res);
            goto error_close_progid_key;
        }
    }

    if (clsid)
    {
        res = register_key_defvalueW (progid_key,
                                      clsid_keyname,
                                      clsid);
        if (res != ERROR_SUCCESS)
        {
            goto error_close_progid_key;
        }
    }

    if (curver_progid)
    {
        res = register_key_defvalueA (progid_key,
                                      curver_keyname,
                                      curver_progid);
        if (res != ERROR_SUCCESS)
        {
            goto error_close_progid_key;
        }
    }

    if (extra)
    {
        HKEY    extra_key;

        res = RegCreateKeyExA (progid_key, extra, 0,
                               NULL,
                               REG_OPTION_NON_VOLATILE,
                               KEY_READ | KEY_WRITE, NULL,
                               &extra_key, NULL);
        if (res == ERROR_SUCCESS)
        {
            RegCloseKey (extra_key);
        }
        else
        {
            create_failed (res);
        }
    }
error_close_progid_key:
    RegCloseKey (progid_key);
out:
    return res;
}

/******************************************************************************
 *      register driver
 *
 * CREATES (if the WineASIO key doesn't exist):
 *   HKEY_LOCAL_MACHINE\Software\ASIO\WineASIO\CLSID\<wine_clsid>
 *   HKEY_LOCAL_MACHINE\Software\ASIO\WineASIO\Description\WineASIO Driver
 */
static HRESULT
register_driver (void)
{
    LPCSTR  asio_key    = "Software\\ASIO\\WineASIO";
    LPCSTR  clsid       = "CLSID";
    LPCSTR  wine_clsid  = "{48D0C522-BFCC-45CC-8B84-17F25F33E6E8}";
    LPCSTR  desc        = "Description";
    LPCSTR  wine_desc   = "WineASIO Driver";
    HKEY    key;
    LONG    rc;

    rc = RegOpenKeyExA (HKEY_LOCAL_MACHINE, asio_key, 0,
                        KEY_READ | KEY_WRITE,
                        &key);
    if (rc == ERROR_SUCCESS)
    {
        goto out;
    }

    rc = RegCreateKeyExA (HKEY_LOCAL_MACHINE, asio_key, 0,
                          NULL,
                          REG_OPTION_NON_VOLATILE,
                          KEY_READ | KEY_WRITE, NULL,
                          &key, NULL);
    if (rc != ERROR_SUCCESS)
    {
        create_failed (rc);
        goto out;
    }

    rc = RegSetValueExA (key, clsid, 0,
                         REG_SZ, (const BYTE *) wine_clsid,
                         strlen(wine_clsid) + 1);
    if (rc != ERROR_SUCCESS)
    {
        set_failed (rc);
        goto out;
    }

    rc = RegSetValueExA (key, desc, 0,
                         REG_SZ, (const BYTE *) wine_desc,
                         strlen(wine_desc) + 1);
    if (rc != ERROR_SUCCESS)
    {
        set_failed (rc);
    }
out:
    RegCloseKey (key);

    return rc;
}

/******************************************************************************
 *      register_interfaces
 */
static HRESULT
register_interfaces (struct regsvr_interface const *list)
{
    LONG    res = ERROR_SUCCESS;
    HKEY    interface_key;

    res = RegCreateKeyExW (HKEY_CLASSES_ROOT, interface_keyname, 0,
                           NULL,
                           REG_OPTION_NON_VOLATILE,
                           KEY_READ | KEY_WRITE, NULL,
                           &interface_key, NULL);
    if (res != ERROR_SUCCESS)
    {
        create_failed (res);
        goto error_return;
    }

    for (; res == ERROR_SUCCESS && list->iid; ++list)
    {
        WCHAR   buf[MAX_GUID_STRING_LEN];
        HKEY    iid_key;

        StringFromGUID2 (list->iid, buf, MAX_GUID_STRING_LEN);

        res = RegCreateKeyExW (interface_key, buf, 0,
                               NULL,
                               REG_OPTION_NON_VOLATILE,
                               KEY_READ | KEY_WRITE, NULL,
                               &iid_key, NULL);
        if (res != ERROR_SUCCESS)
        {
            create_failed (res);
            goto error_close_interface_key;
        }

        if (list->name)
        {
            res = RegSetValueExA (iid_key, NULL, 0,
                                  REG_SZ, (const BYTE*) (list->name),
                                  strlen (list->name) + 1);
            if (res != ERROR_SUCCESS)
            {
                set_failed (res);
                goto error_close_iid_key;
            }
        }

        if (list->base_iid)
        {
            res = register_key_guid (iid_key,
                                     base_ifa_keyname,
                                     list->base_iid);
            if (res != ERROR_SUCCESS)
            {
                goto error_close_iid_key;
            }
        }

        if (list->num_methods > 0)
        {
            static WCHAR const fmt[3] = { '%', 'd', 0 };
            HKEY               key;

            res = RegCreateKeyExW (iid_key, num_methods_keyname, 0,
                                   NULL,
                                   REG_OPTION_NON_VOLATILE,
                                   KEY_READ | KEY_WRITE, NULL,
                                   &key, NULL);
            if (res != ERROR_SUCCESS)
            {
                create_failed (res);
                goto error_close_iid_key;
            }

            wsprintfW (buf, fmt, list->num_methods);

            res = RegSetValueExW (key, NULL, 0,
                                  REG_SZ, (const BYTE *) buf,
                                  (lstrlenW (buf) + 1) * sizeof (WCHAR));

            RegCloseKey (key);

            if (res != ERROR_SUCCESS)
            {
                set_failed (res);
                goto error_close_iid_key;
            }
        }

        if (list->ps_clsid)
        {
            res = register_key_guid (iid_key,
                                     ps_clsid_keyname,
                                     list->ps_clsid);
            if (res != ERROR_SUCCESS)
            {
                goto error_close_iid_key;
            }
        }

        if (list->ps_clsid32)
        {
            res = register_key_guid (iid_key,
                                     ps_clsid32_keyname,
                                     list->ps_clsid32);
            if (res != ERROR_SUCCESS)
            {
                goto error_close_iid_key;
            }
        }

error_close_iid_key:
        RegCloseKey (iid_key);
    }

error_close_interface_key:

    RegCloseKey (interface_key);
error_return:
    return res != ERROR_SUCCESS ? HRESULT_FROM_WIN32 (res) : S_OK;
}

/******************************************************************************
 *      register_coclasses
 *
 * Creates:
 * HKEY_CLASSES_ROOT\CLSID\<clsid>\@\WineASIO Object
 * HKEY_CLASSES_ROOT\CLSID\<clsid>\InProcServer\<list[i]->ips>[NULL] FIXME
 * HKEY_CLASSES_ROOT\CLSID\<clsid>\InProcServer32\@\wineasio.dll
 * HKEY_CLASSES_ROOT\CLSID\<clsid>\InProcServer32\ThreadingModel\Apartment
 */
static HRESULT
register_coclasses (struct regsvr_coclass const *list)
{
    LONG    res = ERROR_SUCCESS;
    HKEY    coclass_key;

    TRACE ("list: %p\n", list);

    res = RegCreateKeyExW (HKEY_CLASSES_ROOT, clsid_keyname, 0,
                           NULL,
                           REG_OPTION_NON_VOLATILE,
                           KEY_READ | KEY_WRITE, NULL,
                           &coclass_key, NULL);
    if (res != ERROR_SUCCESS)
    {
        create_failed (res);
        goto out;
    }

    for (; res == ERROR_SUCCESS && list->clsid; ++list)
    {
        WCHAR   buf[MAX_GUID_STRING_LEN];
        HKEY    clsid_key;

        StringFromGUID2 (list->clsid, buf, MAX_GUID_STRING_LEN);

        res = RegCreateKeyExW (coclass_key, buf, 0,
                               NULL,
                               REG_OPTION_NON_VOLATILE,
                               KEY_READ | KEY_WRITE, NULL,
                               &clsid_key, NULL);
        if (res != ERROR_SUCCESS)
        {
            create_failed (res);
            goto err_out_close_coclass_key;
        }

        if (list->name)
        {
            res = RegSetValueExA (clsid_key, NULL, 0,
                                  REG_SZ, (const BYTE *) (list->name),
                                  strlen (list->name) + 1);
            if (res != ERROR_SUCCESS)
            {
                set_failed (res);
                goto err_out_close_clsid_key;
            }
        }

        if (list->ips)
        {
            res = register_key_defvalueA (clsid_key,
                                          ips_keyname,
                                          list->ips);
            if (res != ERROR_SUCCESS)
            {
                goto err_out_close_clsid_key;
            }
        }

        if (list->ips32)
        {
            HKEY    ips32_key;

            res = RegCreateKeyExW (clsid_key, ips32_keyname, 0,
                                   NULL,
                                   REG_OPTION_NON_VOLATILE,
                                   KEY_READ | KEY_WRITE, NULL,
                                   &ips32_key, NULL);
            if (res != ERROR_SUCCESS)
            {
                create_failed (res);
                goto err_out_close_clsid_key;
            }

            res = RegSetValueExA (ips32_key, NULL, 0,
                                  REG_SZ, (const BYTE *) list->ips32,
                                  lstrlenA (list->ips32) + 1);
            if (res == ERROR_SUCCESS && list->ips32_tmodel)
            {
                res = RegSetValueExA (ips32_key, tmodel_valuename, 0,
                                      REG_SZ, (const BYTE*) list->ips32_tmodel,
                                      strlen (list->ips32_tmodel) + 1);
            }

            RegCloseKey (ips32_key);

            if (res != ERROR_SUCCESS)
            {
                set_failed (res);
                goto err_out_close_clsid_key;
            }
        }

        if (list->progid)
        {
            res = register_key_defvalueA (clsid_key,
                                          progid_keyname,
                                          list->progid);
            if (res != ERROR_SUCCESS)
            {
                goto err_out_close_clsid_key;
            }

            res = register_progid (buf,
                                   list->progid,
                                   NULL,
                                   list->name,
                                   list->progid_extra);
            if (res != ERROR_SUCCESS)
            {
                goto err_out_close_clsid_key;
            }
        }

        if (list->viprogid)
        {
            res = register_key_defvalueA (clsid_key,
                                          viprogid_keyname,
                                          list->viprogid);
            if (res != ERROR_SUCCESS)
            {
                goto err_out_close_clsid_key;
            }

            res = register_progid (buf,
                                   list->viprogid,
                                   list->progid,
                                   list->name,
                                   list->progid_extra);
            if (res != ERROR_SUCCESS)
            {
                goto err_out_close_clsid_key;
            }
        }
err_out_close_clsid_key:
        RegCloseKey (clsid_key);
    }

err_out_close_coclass_key:
    RegCloseKey (coclass_key);

out:
    return res != ERROR_SUCCESS ? HRESULT_FROM_WIN32 (res) : S_OK;
}


/******************************************************************************
 *      Class and Interface definitions
 */
static struct regsvr_coclass const coclass_list[] =
{
    {
        &CLSID_WineASIO,
        "WineASIO Object",
        NULL,
        "wineasio.dll",
        "Apartment"
    },
    {
        NULL                    /* list terminator */
    }
};

static struct regsvr_interface const interface_list[] =
{
    {
        NULL                    /* list terminator */
    }
};


/******************************************************************************
 *      Entrypoints
 */

/******************************************************************************
 *      DllUnregisterServer (wineasio.@)
 */
HRESULT WINAPI
DllUnregisterServer (void)
{
    HRESULT hr;

    TRACE ("\n");

    hr = unregister_coclasses (coclass_list);
    if (!SUCCEEDED (hr))
    {
        goto out;
    }

    hr = unregister_interfaces (interface_list);
    if (!SUCCEEDED (hr))
    {
        goto out;
    }

    hr = unregister_driver ();
out:
    return hr;
}

/******************************************************************************
 *      DllRegisterServer (wineasio.@)
 */
HRESULT WINAPI
DllRegisterServer (void)
{
    HRESULT hr;

    TRACE("\n");

    hr = register_coclasses (coclass_list);
    if (!SUCCEEDED (hr))
    {
        goto out;
    }

    hr = register_interfaces (interface_list);
    if (!SUCCEEDED (hr))
    {
        goto out;
    }

    hr = register_driver ();
out:
    return hr;
}
