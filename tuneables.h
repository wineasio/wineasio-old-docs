/*
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

#ifndef __WINEASIO_TUNEABLES_H__
#define __WINEASIO_TUNEABLES_H__

/******************************************************************************
 *      WineASIO Defaults
 *
 *      Change these values to taste if you want to hardcode your defaults
 *      and thus avoid having to pass parameters through the environment
 *      or set these values from the registry at run-time.
 *
 *      FIXME: how to construct env. var names.
 */

/*
 * Default number of writable JACK input ports FIXME: writable input?
 * on the Windows client application.
 */
#define DEFAULT_WINEASIO_NUM_PORTS_INP 16

/*
 * Default number of readable JACK output ports
 * on the Windows client application.
 */
#define DEFAULT_WINEASIO_NUM_PORTS_OUT 16

/*
 * Default boolean indicating whether the JACK
 * buffer size should be fixed and unchangeable.
 */
// FIXME: where does the value come from if fixed?
#define DEFAULT_WINEASIO_FIXED_BUFSZ 1

/*
 * Default preferred buffer size.
 */
// FIXME when is this used?
#define DEFAULT_WINEASIO_PREFERD_BUFSZ 2048

#define DEFAULT_WINEASIO_CONN_PORTS 1
#define DEFAULT_WINEASIO_START_JACK 1


/******************************************************************************
 * YOU SHOULDN'T HAVE TO CHANGE THESE UNLESS YOU REALLY KNOW WHAT YOU'RE DOING
 */
// FIXME: comment these.
#define WINEASIO_MIN_BUFSZ    16
#define WINEASIO_MAX_BUFSZ    8192

/*
 * Sample format to be used by JACK. FIXME
 *
 * Valid values are:
 *   - ASIOSTFloat32LSB
 *   - ASIOSTInt32LSB
 */
#define WINEASIO_SMPL_FMT ASIOSTFloat32LSB

#define WINEASIO_MAX_NAME_LEN 32
#define WINEASIO_MAX_ENV_LEN  6              /* max len of env. value (incl. tr. NULL) */

#define WINEASIO_VERSION      95

#define IEEE754_64FLOAT 1

// FIXME: put NATIVE IN HERE
#endif
