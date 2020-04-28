#define MAX_ISOS 2000

static int  nn = 0;
static int  nt[MAX_ISOS];
static char ff[MAX_ISOS][255];

static char subpath[MAX_PATH_LEN];
/*
void addlog(char *msg1, char *msg2)
{
	FILE* f;
	f = fopen("/dev_hdd0/prepiso2.log", "a+b");
	if(f)
	{
		char msg[300];
		sprintf(msg, "%s %s\r\n", msg1, msg2);
		fwrite(msg, 1, strlen(msg), f);
		fclose (f);
	}
}
*/
static int s_mode;

int copy_exfat(char *src_file, char *out_file, u64 size)
{
	if(size == 0) return FAILED;

	FIL fd;	   // File objects

	if (!f_open (&fd, src_file, FA_READ))
	{
		char *mem = NULL;

		if((mem = malloc(size)) != NULL)
		{
			f_lseek (&fd, 0);
			UINT re;
			f_read (&fd, (void *)mem, size, &re);
			f_close (&fd);

			SaveFile(out_file, mem, re);

			free(mem);

			return (re == size) ? SUCCESS : FAILED;
		}
		else
		{
			f_close (&fd);
		}
	}
	return FAILED;
}

//dir contents
static int dir_read (char *dpath)
{
	nn = 0;

	/* Register work area to the default drive */
	FATFS fs;   /* Work area (filesystem object) for logical drive */
	char drn[5];
	snprintf(drn, 4, "%.3s", dpath);
	f_mount(&fs, drn, 0);

	char fn[256];

	FDIR dir;
	FRESULT res;

	res = f_opendir(&dir, dpath);					   /* Open the directory */
	if (res == FR_OK)
	{
		bool is_iso;
		static FILINFO fno;
		for(;;)
		{
			FRESULT res1 = f_readdir(&dir, &fno);	   /* Read a directory item */
			if (res1 != FR_OK || fno.fname[0] == 0)
			{
				break;
			}
			if (fno.fattrib & AM_DIR)
			{				/* scan directories recursively */
				if(fno.fname[0] == '.') continue;
				snprintf (subpath, 255, "%s", fno.fname);
				snprintf (fn, 255, "%s/%s", dpath, fno.fname);
				dir_read (fn);
			}
			else if(s_mode == 1)
			{
				int flen = strlen(fno.fname);
				if(flen < 4) continue; flen -= 4;
				char *ext = fno.fname + flen;

				//--- create .ntfs[BDFILES] for 4="VIDEO", 5="MOVIES", 6="PKG", 7="Packages", 8="packages", 9="BDFILE", 10="PS2ISO", 10="PSPISO"
				if(g_mode >= 4)
				{
					snprintf (fn, 255, "%s/%s", dpath, fno.fname);
					make_fake_iso(g_mode, ext, fno.fname, fn, device_id, fno.fsize);
					continue;
				}
				//---------------

				//--- is ISO?
				is_iso =	( (strcasestr(ext, ".iso")) ) ||
				(g_mode && (( (strcasestr(ext, ".bin")) ) ||
							( (strcasestr(ext, ".img")) ) ||
							( (strcasestr(ext, ".mdf")) ) ));

				if(is_iso)
				{
					cd_sector_size = 2352;
					cd_sector_size_param = 0;
					num_tracks = 1;

					if(g_mode == PSXISO)
					{
						FIL fd;
						int ret = 0;

						// detect CD sector size
						if(!f_open (&fd, fn, FA_READ))
						{
							char buffer[20];
							u16 sec_size[7] = {2352, 2048, 2336, 2448, 2328, 2340, 2368};
							for(u8 n = 0; n < 7; n++)
							{
								f_lseek (&fd, ((sec_size[n]<<4) + 0x18));
								UINT lbr;
								f_read (&fd, (void *)buffer, 20, &lbr);
								if(  (memcmp(buffer + 8, "PLAYSTATION ", 0xC) == 0) ||
									((memcmp(buffer + 1, "CD001", 5) == 0) && buffer[0] == 0x01) ) {cd_sector_size = sec_size[n]; break;}
							}
							f_close(&fd);

							if(cd_sector_size & 0xf) cd_sector_size_param = cd_sector_size<<8;
							else if(cd_sector_size != 2352) cd_sector_size_param = cd_sector_size<<4;
						}

						// parse CUE file
						int path_len = snprintf (fn, 255, "%s/%s", dpath, fno.fname);
						strcpy(fn + path_len - 3, "CUE");
						if(f_open (&fd, fn, FA_READ))
						{
							strcpy(fn + path_len - 3, "cue");
							if(f_open (&fd, fn, FA_READ)) ret = -1;
						}

						if (ret >= 0)
						{
							UINT r = 0;
							f_read (&fd, (char *)cue_buf, sizeof(cue_buf), &r);
							f_close(&fd);

							if (r > 0)
							{
								char dummy[64];

								if (cobra_parse_cue(cue_buf, r, tracks, 100, &num_tracks, dummy, sizeof(dummy)-1) != 0)
								{
									num_tracks = 1;
								}
							}
						}
					}

					// store path for conversion after the directory scan (fflib_file_to_sectors breaks the scan)
					nt[nn] = num_tracks;
					snprintf (ff[nn], 255, "%s", fno.fname);
					nn++; if(nn >= MAX_ISOS) break;
				}
			}
			else if(s_mode == 0)
			{
				if(strstr(fno.fname, ".png") || strstr(fno.fname, ".PNG") || strstr(fno.fname, ".jpg") || strstr(fno.fname, ".JPG") || strstr(fno.fname, ".SFO"))
				{
					if(*subpath)
						snprintf (wm_path, 255, "/dev_hdd0/tmp/wmtmp/[%s] %s", subpath, fno.fname);
					else
						snprintf (wm_path, 255, "/dev_hdd0/tmp/wmtmp/%s", fno.fname);

					if((file_exists(wm_path) == false) && (fno.fsize < 4194304))
					{
						snprintf(fn, 255, "%s/%s", dpath, fno.fname);
						copy_exfat(fn, wm_path, fno.fsize);
					}
				}
			}
		}
		f_closedir(&dir);
	}
	//
	if(nn)
	{
		int flen, dlen;
		for(dlen = strlen(dpath); dlen; dlen--) if(dpath[dlen] == '/') break; // find subdir name

		char *subdir = &dpath[dlen + 1]; sprintf(subpath, "%s", dpath);

		for(int f = 0; f < nn; f++)
		{
			for(int i = 0; i < MAX_SECTIONS; i++) sections[i] = sections_size[i] = 0;

			flen = snprintf (fn, 255, "%s/%s", subpath, ff[f]);

			int parts = fflib_file_to_sectors (fn, sections, sections_size, MAX_SECTIONS, 1);
			if (parts >= MAX_SECTIONS) continue;

			if(dlen >= 10)
				flen = snprintf (wm_path, 255, "[%s] %s", subdir, ff[f]);
			else
				flen = snprintf (wm_path, 255, "%s", ff[f]);

			wm_path[flen - 4] = 0;

			num_tracks = nt[f];
			build_file(wm_path, parts, num_tracks, device_id, g_profile, g_mode);
		}
		nn = 0;
	}
	//
	f_mount(0, drn, 0);
	//
	return res;
}

static void scan_exfat(void)
{
	fflib_init();
	for(u8 port = 0; port < 8; port++)
	{
		device_id = USB_MASS_STORAGE(port);
		int ret = fflib_attach (port, device_id, 1);
		if (ret == FR_OK)
		{
			for (u8 profile = 0; profile < 6; profile++)
			{
				g_profile = profile;
				for(u8 m = 0; m < 12; m++) //0="PS3ISO", 1="BDISO", 2="DVDISO", 3="PSXISO", 4="VIDEO", 5="MOVIES", 6="PKG", 7="Packages", 8="packages", 9="BDFILE", 10="PS2ISO", 10="PSPISO"
				{
					g_mode = m;
					*subpath = 0;

					snprintf(path, sizeof(path), "%u:/%s%s", port, c_path[m], SUFIX(profile));
					for(s_mode = 0; s_mode < 2; s_mode++)
						dir_read (path);
				}
			}
		}
		fflib_detach (port);
	}
}
