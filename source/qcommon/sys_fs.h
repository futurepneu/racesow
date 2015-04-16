/*
Copyright (C) 2007 Pekka Lampila

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef __SYS_FS_H
#define __SYS_FS_H

const char *Sys_FS_GetHomeDirectory( void );
const char *Sys_FS_GetCacheDirectory( void );
const char *Sys_FS_GetSecureDirectory( void );

qboolean    Sys_FS_RemoveDirectory( const char *path );
qboolean    Sys_FS_CreateDirectory( const char *path );

const char *Sys_FS_FindFirst( const char *path, unsigned musthave, unsigned canthave );
const char *Sys_FS_FindNext( unsigned musthave, unsigned canthave );
void	    Sys_FS_FindClose( void );

void		*Sys_FS_LockFile( const char *path );
void	    Sys_FS_UnlockFile( void *handle );

time_t		Sys_FS_FileMTime( const char *filename );

// virtual storage of pack files, such as .obb on Android
void		Sys_VFS_Init( void );
void		Sys_VFS_TouchGamePath( const char *gamedir, qboolean initial );
char		**Sys_VFS_ListFiles( const char *basepath, const char *gamedir, const char *extension, int *numfiles );
void		*Sys_VFS_FindFile( const char *filename );
const char	*Sys_VFS_VFSName( void *handle ); // must return null for null handle
unsigned	Sys_VFS_FileOffset( void *handle ); // ditto
unsigned	Sys_VFS_FileSize( void *handle ); // ditto
void		Sys_VFS_Shutdown( void );

#endif // __SYS_FS_H
