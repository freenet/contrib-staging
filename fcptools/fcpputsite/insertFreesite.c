/*
  insertFreesite.c - part of fcpputsite

  This module is the engine that does the inserting
  CopyLeft (c) 2001 by David McNab
*/


#include "ezFCPlib.h"
#include "compat.h"
#include "fcpputsite.h"


//
// IMPORTED DECLARATIONS
//

extern int      fcpLogCallback(int level, char *buf);
extern SiteFile *scan_dir(char *dirname, int *pNumFiles);
extern char     *strsav(char *old, int * len, char *text_to_append);
extern char     *GetMimeType(char *pathname);

//
// EXPORTED DECLARATIONS
//

int insertFreesite(char *_siteName, char *siteDir, char *pubKey, char *privKey,
                    char *defaultFile, int daysFuture, int maxThreads,
                    int maxretries, int dodbr);


//
// PRIVATE DECLARATIONS
//

static void putsiteThread(void *arg);

static int  numFiles;
static int  maxTries;
static char *  siteName;
static char *  m_siteName;

static int  defaultIndex;
static int	jobsFailed = 0;
static int	jobsProcessed = 0;
static int  jobsToDo = 0;


////////////////////////////////////////////////////////////////
// END OF DECLARATIONS
////////////////////////////////////////////////////////////////
int checkjob(PutJob *job) {
	int found=0;
	time_t lstart_time=time(NULL);

	switch (job->threadStatus) {
		case INSERT_THREAD_FAILED:
			jobsFailed++;
		case INSERT_THREAD_DONE:
			jobsProcessed++;
		case INSERT_THREAD_IDLE:
			job->threadStatus = INSERT_THREAD_IDLE;
			return 1;
			break;
		case INSERT_THREAD_RUNNING:
#if 0
			if (( job->starttime + insertTimeout ) < lstart_time ) {
				_fcpLog(FCP_LOG_CRITICAL, "Cancelling thread operating on %s (timeout after %d seconds)", 
					job->fileSlot->filename,  lstart_time - job->starttime);
				crCancelThread(job->ticket);
				if (job->fileSlot->insertStatus == INSERT_FILE_SAVEDATA) {
					if (job->threadStatus != INSERT_THREAD_IDLE) {
						_fcpLog(FCP_LOG_CRITICAL, "fileSlot DONE but thread not IDLE for %s",
								job->fileSlot->filename);
						job->threadStatus = INSERT_THREAD_IDLE;
					}
					return 1;
				}
				job->fileSlot->insertStatus = INSERT_FILE_WAITING;
				job->threadStatus = INSERT_THREAD_IDLE;
				return 1;
			}
#endif
			break;
	}
	return 0;
}

int writekey(SiteFile * file) {
	FILE * fp;
	FILE * nfp;
	char buf[1024];
	char nfp_n[1024];
	char fp_n[1024];
	char *key;
	char *s_time;
	char *filename;

	if (file->insertStatus != INSERT_FILE_SAVEDATA)
		exit(-1);
	

	jobsToDo--;
	snprintf(fp_n, 1024, "/tmp/%s", m_siteName);
	fp=fopen(fp_n, "r");
	snprintf(nfp_n, 1024, "/tmp/%s.new", m_siteName);
	nfp=fopen(nfp_n, "w");
	if (!nfp)
		return -1;
	if (fp) { // check for existant entries
		while(fgets(buf, 1024, fp)) {
			/* CHK, insert time in hex, filename */
			buf[strlen(buf)]=0;
			filename=buf+CHKKEYLEN+18;
			if (strcmp(filename, file->filename))	 /* This is to be reoutputted. */
				fputs(buf, nfp);
		}
		fclose(fp);
	}

	_fcpLog(FCP_LOG_VERBOSE, "%s:%08x:%08x:%08x:%s\n", 
		file->chk, time(NULL), file->ctime, file->size, file->filename);
	fprintf(nfp, "%s:%08x:%08x:%08x:%s\n", 
		file->chk, time(NULL), file->ctime, file->size, file->filename);
	fclose(nfp);
	rename(nfp_n, fp_n);
	
	file->insertStatus = INSERT_FILE_DONE;
	return(0);
}

int file_compar(const void *_a, const void *_b) {
	const SiteFile *a=*(const SiteFile **) _a;
	const SiteFile *b=*(const SiteFile **) _b;
	return strcmp(a->filename, b->filename);
}

int file_compar2(const void *_a, const void *_b) {
	const char *filename=_a;
	const SiteFile *b=*(const SiteFile **) _b;
	return strcmp(filename, b->filename);
}

int freshen_files(SiteFile * file, int numfiles) {
	SiteFile ** sf;
	FILE *fp;
	int i;
	char *chk;
	char *filename;
	char buf[1024];

	jobsToDo=numfiles;

	_fcpLog(FCP_LOG_VERBOSE, "Why am i here?\n");
	sprintf(buf, "/tmp/%s", m_siteName);
	fp=fopen(buf, "r");
	if (fp) {
		sf=safeMalloc(numfiles * sizeof(*sf));
		for (i=0; i<numfiles; i++) {
			sf[i]=&file[i];
		}
		qsort(sf, numfiles, sizeof(*sf), &file_compar);

		while(fgets(buf, 1024, fp)) {
			SiteFile **_tf;
			char *s_puttime;
			char *s_ctime;
			char *s_size;

			buf[strlen(buf)-1]=0;
			chk=buf;
			s_puttime=chk+66;
			*s_puttime=0;
			s_puttime++;
			s_ctime=s_puttime+8;
			*s_ctime=0;
			s_ctime++;
			s_size=s_ctime+8;
			*s_size=0;
			s_size++;
			filename=s_size+8;
			*filename=0;
			filename++;
			_tf=(SiteFile **) bsearch(filename, sf, numfiles, sizeof(*sf), &file_compar2);

			if (_tf) {
				time_t puttime;
				int ctime;
				unsigned int size;
				SiteFile *tf=*_tf;

				sscanf(s_ctime, "%08x", &ctime);
				sscanf(s_puttime, "%08x", &puttime);
				sscanf(s_size, "%08x", &size);
				if ((tf->ctime==ctime) && 
						((tf->size < (256 * 1024)) ||
						(size == tf->size))) {
					if  (tf->insertStatus != INSERT_FILE_DONE) {
						strcpy(tf->chk, chk);
						tf->insertStatus=INSERT_FILE_DONE;
//						_fcpLog(FCP_LOG_VERBOSE, "\t%s: last updated %d seconds ago as: %s",
//							tf->filename, time(NULL) - puttime, tf->chk);
						jobsToDo--;
					} // or skip it.
				} else  {
					_fcpLog(FCP_LOG_VERBOSE, "\t%s: Changed, re-uploading.",
							tf->filename);
							
				}
			}
			
		}
		free(sf);
		fclose(fp);
	}
	return(0);
}


int insertFreesite(char *_siteName, char *siteDir, char *pubKey, char *privKey,
                   char *defaultFile, int daysFuture, int maxThreads,
                   int maxAttempts, int dodbr)
{
    SiteFile *files;
    int     i;
    char    *s;
    PutJob  *job;
    int     running = 1;
	int		clicks = 0;

    maxTries = maxAttempts;
	siteName = _siteName;
	m_siteName = strdup(siteName);
	s=m_siteName;
	while (*s) {
		if (*s == '/')
			*s='_';
		*s++;
	}
	s=NULL;

    // truncate trailing '/' from dir if any
    s = siteDir + strlen(siteDir) - 1;
    if (*s == '/' || *s == '\\')
        *s = '\0';

    // scan the directory recursively and get a list of files
    if ((files = scan_dir(siteDir, &numFiles)) == NULL)
    {
        _fcpLog(FCP_LOG_CRITICAL, "insertFreesite: can't read directory '%s'", siteDir);
        return -1;
    }

	defaultIndex=-1;
    // ensure default file actually exists
    for (i = 0; i < numFiles; i++)
    {
        if (!strcmp(defaultFile, files[i].relpath))
            defaultIndex = i;
		files[i].retries=0;
    }
    if (defaultIndex == -1)
    {
        _fcpLog(FCP_LOG_CRITICAL, "FATAL: default file '%s' not found", defaultFile);
        free(files);
        return -1;
    }
	freshen_files(files, numFiles);
#if 0
    // ok - we can go ahead with the job
    _fcpLog(FCP_LOG_NORMAL, "--------------------------------------------------");
    _fcpLog(FCP_LOG_NORMAL, "Inserting site:   %s", siteName);
    _fcpLog(FCP_LOG_NORMAL, "--------------------------------------------------");
    _fcpLog(FCP_LOG_NORMAL, "Directory:        %s", siteDir);
    _fcpLog(FCP_LOG_NORMAL, "Public Key:       %s", pubKey);
    _fcpLog(FCP_LOG_NORMAL, "Private Key:      %s", privKey);
    _fcpLog(FCP_LOG_NORMAL, "Default file:     %s", defaultFile);
    _fcpLog(FCP_LOG_NORMAL, "Days ahead:       %d", daysFuture);
    _fcpLog(FCP_LOG_NORMAL, "DBR:              %s", dodbr ? "yes" : "no");
    _fcpLog(FCP_LOG_NORMAL, "Maximum threads:  %d", maxThreads);
    _fcpLog(FCP_LOG_NORMAL, "Maximum attempts: %d", maxAttempts);

    // print out list of files
    _fcpLog(FCP_LOG_VERBOSE, "Files in directory '%s' are:", siteDir);
    for (i = 0; i < numFiles; i++)
        _fcpLog(FCP_LOG_VERBOSE, " %9d %s", files[i].size, files[i].relpath);
#endif

    //
    // Now insert all these files
    //

    // create and initialise status tables
    job = safeMalloc(maxThreads * sizeof(*job));
    for (i = 0; i < maxThreads; i++) {
        job[i].threadStatus = INSERT_THREAD_IDLE;
	}

    // the big loop - search for free thread slots and dispatch insert threads till all done
    while (running)
    {
		time_t lstart_time = time(NULL);
        int firstWaitingFile;
		static int minWaiting=0;
		static int waitjump;
        int threadSlot;
		PutJob * jptr;

		clicks++;



        // search for waiting files
		waitjump=1;
        for (firstWaitingFile = minWaiting; firstWaitingFile < numFiles;
			   	firstWaitingFile++) {
			if (files[firstWaitingFile].insertStatus == INSERT_FILE_SAVEDATA) {
				writekey(files + firstWaitingFile);
				files[firstWaitingFile].insertStatus == INSERT_FILE_DONE;
			}
			if (files[firstWaitingFile].insertStatus != INSERT_FILE_DONE)
				waitjump=0;
			if (files[firstWaitingFile].insertStatus == INSERT_FILE_DONE) {
				if (waitjump)
					minWaiting=firstWaitingFile; // highwatermark.
			}
            if (files[firstWaitingFile].insertStatus == INSERT_FILE_WAITING)
                break;
		}
		
		_fcpLog(FCP_LOG_VERBOSE, "%d of %d files left",
				jobsToDo, numFiles);


        if (firstWaitingFile == numFiles) {
		    for (i = 0; i < maxThreads; i++)
				if (( job[i].threadStatus != INSERT_THREAD_IDLE) && 
					( job[i].threadStatus != INSERT_THREAD_DONE))
				   break;
		    if (i == maxThreads) {
				// all idle, they'd be failed so no race.
				// We're done.  YAAY!
			   break;
			}
			threadSlot = maxThreads;
		} else {

	        // search for a thread slot
		    for (threadSlot = 0; threadSlot < maxThreads; threadSlot++)
				if (checkjob(job + threadSlot))
					break;
		}

        // any threads available yet?
        if (threadSlot == maxThreads)
        {
			static int bclicks=0;
            // no - wait a while and restart
			bclicks++;
			if (!(bclicks % 180)) {
				_fcpLog(FCP_LOG_DEBUG, "fcpputsite: all thread slots full");
			}
			if (!(bclicks % 5)) { // every Nth iteration
				_fcpLog(FCP_LOG_NORMAL, "Busy clicks: %d", bclicks);
				_fcpLog(FCP_LOG_DEBUG, "Id Status  Time Filename");
				for (i=0; i < maxThreads; i++) {
					int status = job[i].threadStatus;
					static char *status_str[4] = { "Idle", "Running", "Done", "Failed" };
					_fcpLog(FCP_LOG_DEBUG, "%2d%8s %4d %s", i,
						status_str[status],
						(status == INSERT_THREAD_RUNNING ) ? lstart_time - job[i].starttime : 0,
						(status == INSERT_THREAD_RUNNING ) ? job[i].fileSlot->filename : "");
				}
			}
            crSleep( 2, 0 );
            continue;
        }

		_fcpLog(FCP_LOG_DEBUG, "fcpputsite: found thread slot for inserting %s",
			strrchr(files[firstWaitingFile].filename, '/')+1);

        // Fill out a job form and launch thread
        //numthreads++;
        files[firstWaitingFile].insertStatus = INSERT_FILE_INPROG;
        job[threadSlot].fileSlot = files + firstWaitingFile;
        job[threadSlot].metadata = NULL;
        job[threadSlot].key[0] = '\0';
        job[threadSlot].threadStatus = INSERT_THREAD_RUNNING;
		job[threadSlot].starttime = time(NULL);
		jptr=job + threadSlot;
        crLaunchThread(putsiteThread, (void *) jptr);
        //sucksite_thread(pPutJob);

    }			// 'while (inserting files)'


	_fcpLog(FCP_LOG_DEBUG, "fcpputsite: broke main insert loop");

   // All files are either in progress or done - wait for all to complete


    // did file inserts all succeed?
    for (i = 0; i < numFiles; i++)
    {
        if (files[i].insertStatus != INSERT_FILE_DONE)
        {
			if (files[i].insertStatus != INSERT_FILE_FAILED)
				_fcpLog(FCP_LOG_CRITICAL, "Insane status for '%s'",
						files[i].relpath);
			else
				_fcpLog(FCP_LOG_CRITICAL, "Failed to insert '%s'",
					   files[i].relpath);
        }
    }

    if (jobsFailed)
    {
        _fcpLog(FCP_LOG_CRITICAL, "One or more inserts failed - aborting");
        return -1;
    }
    else
    {
        char *metaRoot;                                 // metadata for MSK root key
        char *metaMap;                                  // metadata for site map key
        char *metaChk;

        char dbrRootUri[128];
        char dbrTargetUri[128];
        char mapChkUri[128];

        int mapLen;
        HFCP *hfcp;

        time_t timeNow;

        hfcp = fcpCreateHandle();

		_fcpLog(FCP_LOG_VERBOSE,
			"insertFreesite: file inserts succeeded, inserting metadata");

        // create a DBR root
        sprintf(dbrRootUri, "SSK@%s/%s", privKey, siteName);

        metaRoot = strsav(NULL, NULL,
                        "Version\nRevision=1\nEndPart\n"
                        "Document\nDateRedirect.Target=freenet:SSK@");
        metaRoot = strsav(metaRoot, NULL, pubKey);
        metaRoot = strsav(metaRoot, NULL, "/");
        metaRoot = strsav(metaRoot, NULL, siteName);
        metaRoot = strsav(metaRoot, NULL, "\nEnd\n");

        // create dbr target uri
		if (dodbr)
		{
            time(&timeNow);
            sprintf(dbrTargetUri, "SSK@%s/%lx-%s",
                privKey,
                (timeNow - (timeNow % 86400)) + (daysFuture * 86400),
                siteName);
        }
        else
            strcpy(dbrTargetUri, dbrRootUri);

        // create mapfile
		mapLen=0;
        metaMap = strsav(NULL, &mapLen, "Version\nRevision=1\nEndPart\n");
        for (i = 0; i < numFiles; i++)
        {
            metaMap = strsav(metaMap, &mapLen, "Document\nName=");
            metaMap = strsav(metaMap, &mapLen, files[i].relpath);
            metaMap = strsav(metaMap, &mapLen, "\nInfo.Format=");
            metaMap = strsav(metaMap, &mapLen, GetMimeType(files[i].relpath));
            metaMap = strsav(metaMap, &mapLen, "\nRedirect.Target=");
            metaMap = strsav(metaMap, &mapLen, files[i].chk);
            metaMap = strsav(metaMap, &mapLen, "\n");

            if (i == defaultIndex)
            {
                // Create an unnamed cdoc for default file
                metaMap = strsav(metaMap, &mapLen, "EndPart\n");
                metaMap = strsav(metaMap, &mapLen, "Document\n");
                metaMap = strsav(metaMap, &mapLen, "Info.Format=");
                metaMap = strsav(metaMap, &mapLen, GetMimeType(files[i].relpath));
                metaMap = strsav(metaMap, &mapLen, "\nRedirect.Target=");
                metaMap = strsav(metaMap, &mapLen, files[i].chk);
                metaMap = strsav(metaMap, &mapLen, "\n");
            }

            if (i + 1 < numFiles)
                // not the last cdoc - need an 'EndPart'
                metaMap = strsav(metaMap, &mapLen, "EndPart\n");

        }       // 'for (each file written to mapfile)'
        metaMap = strsav(metaMap, &mapLen, "End\n");

        _fcpLog(FCP_LOG_NORMAL, "METADATA IS %d BYTES LONG", mapLen);
//		_fcpLog(FCP_LOG_DEBUG, "METADATA Follows: \n%s", metaMap);

		// insert DBR root
        if (dodbr && fcpPutKeyFromMem(hfcp, dbrRootUri, NULL, metaRoot, 0) != 0)
        {
            _fcpLog(FCP_LOG_CRITICAL, "Failed to insert DBR root - aborting");
            return 1;
        }

        if (mapLen <= 32767)
//        if (mapLen <= 1)
        {
            // make and insert normal mapfile at dbr target
            if (_fcpPutKeyFromMem(hfcp, dbrTargetUri, NULL, metaMap, 0, mapLen) != 0)
            {
                _fcpLog(FCP_LOG_CRITICAL, "Failed to insert mapfile at '%s'", dbrTargetUri);
                return -1;
            }
        }
        else
        {
            // too big for an ssk, insert map as a chk and redirect to that
            _fcpLog(FCP_LOG_CRITICAL, "Metadata map at %d bytes is too large - adding CHK step", mapLen);
            fcpDestroyHandle(hfcp);
            hfcp = fcpCreateHandle();
            if (fcpPutKeyFromMem(hfcp, "CHK@", NULL, metaMap, 0) != 0)
            {
                _fcpLog(FCP_LOG_CRITICAL, "Failed to insert large mapfile as CHK");
                return -1;
            }

            // inserted ok as CHK - now build a redirect to that CHK
            metaChk = strsav(NULL, NULL,
                            "Version\nRevision=1\nEndPart\n"
                            "Document\nRedirect.Target=");
            metaChk = strsav(metaChk, NULL, hfcp->created_uri);
            metaChk = strsav(metaChk, NULL, "\nEnd\n");

            fcpDestroyHandle(hfcp);

            hfcp = fcpCreateHandle();
    
            // now insert at DBR target a redirect to mapfile CHK
            if (fcpPutKeyFromMem(hfcp, dbrTargetUri, NULL, metaChk, 0) != 0)
            {
                _fcpLog(FCP_LOG_CRITICAL, "Failed to insert large mapfile as CHK");
                return -1;
            }
        }

        // should be ok now

        _fcpLog(FCP_LOG_NORMAL, "============================================");
        _fcpLog(FCP_LOG_NORMAL, "Successfully inserted SSK@%s/%s// - %d days ahead",
                                pubKey, siteName, daysFuture);
        return 0;
    }
}               // 'insertFreesite()'


void putsiteThread(void *arg)
{
    PutJob *job = (PutJob *)arg;
    HFCP *hfcp = fcpCreateHandle();
	char *mimetype;
    int status;
    int i;
    char meta[256];

	mimetype=GetMimeType(job->fileSlot->filename);
	if (mimetype)
		strncpy(hfcp->mimeType, mimetype, L_MIMETYPE);

    sprintf(meta, "Version\nRevision=1\nEndPart\nDocument\nInfo.Format=%s\nEnd\n",
                hfcp->mimeType);

    if (job->metadata == NULL)
    {
        // insert a file
        _fcpLog(FCP_LOG_VERBOSE, "inserting '%s'", job->fileSlot->relpath);
        for (i = job->fileSlot->retries; i < maxTries; i++)
        {
            if (i > 0)
                _fcpLog(FCP_LOG_NORMAL, "retry %d for file %s", i, job->fileSlot->filename);

            if ((status = fcpPutKeyFromFile(hfcp, "CHK@", job->fileSlot->filename, meta)) == 0)
            {
                // successful insert
                strcpy(job->fileSlot->chk, hfcp->created_uri);
                _fcpLog(FCP_LOG_NORMAL, "Successfully inserted %s with %d retries",
                                        job->fileSlot->filename, i);
                break;
            }
        }
        if (status != 0)
        {
            _fcpLog(FCP_LOG_CRITICAL, "Insert failed after %d retries: %s", maxTries, job->fileSlot->filename);
            job->fileSlot->insertStatus = INSERT_FILE_FAILED;
			job->threadStatus = INSERT_THREAD_FAILED;
        } else {
			job->fileSlot->insertStatus = INSERT_FILE_SAVEDATA;
			job->threadStatus = INSERT_THREAD_DONE;
		}
    }
    else
    {
        // inserting metadata against a given key
        _fcpLog(FCP_LOG_VERBOSE, "Inserting metadata at %s", job->key);
        _fcpLog(FCP_LOG_DEBUG, job->metadata);

        for (i = job->fileSlot->retries; i < maxTries; i++)
        {
            if (i > 0)
                _fcpLog(FCP_LOG_NORMAL, "retry %d for key %s", i, job->metadata);

            if ((status = fcpPutKeyFromMem(hfcp, job->key, NULL, job->metadata, 0)) == 0)
            //if ((status = 0) == 0)
            {
                // successful insert
                _fcpLog(FCP_LOG_NORMAL, "Successfully inserted key %s with %d retries",
                                        job->key, i);
                break;
            }
        }
        if (status != 0)
        {
            _fcpLog(FCP_LOG_CRITICAL, "Insert failed after %d retries: %s", maxTries, job->key);
			job->threadStatus = INSERT_THREAD_FAILED;
        } else 
	        job->threadStatus = INSERT_THREAD_DONE;
    }
    //free(job);
}               // 'putsiteThread()'
