/*
 * Citizen Systems
 *
 * CUPS Filter
 *
 * compile cmd: gcc -Wl,-rpath,/usr/lib -Wall -fPIC -O2 -o rastertocbm1k rastertocbm1k.c -lcupsimage -lcups
 * compile requires cups-devel-1.1.19-13.i386.rpm (version not neccessarily important?)
 * find cups-devel location here: http://rpmfind.net/linux/rpm2html/search.php?query=cups-devel&submit=Search+...&system=&arch=
 */

/*
 * Copyright (C) 2006	Citizen Systems Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 * Include necessary headers...
 */

#include	<cups/cups.h>
#include	<cups/ppd.h>
#include	<cups/raster.h>
#include	<stdlib.h>
#include	<fcntl.h>

#ifdef		RPMBUILD

#include	<dlfcn.h>

typedef	cups_raster_t*	(*cupsRasterOpen_fndef)			(int fd,cups_mode_t mode);
typedef	unsigned		(*cupsRasterReadHeader_fndef)	(cups_raster_t *r,cups_page_header_t *h);
typedef	unsigned		(*cupsRasterReadPixels_fndef)	(cups_raster_t *r,unsigned char *p,unsigned len);
typedef	void			(*cupsRasterClose_fndef)		(cups_raster_t *r);

static	cupsRasterOpen_fndef		cupsRasterOpen_fn;
static	cupsRasterReadHeader_fndef	cupsRasterReadHeader_fn;
static	cupsRasterReadPixels_fndef	cupsRasterReadPixels_fn;
static	cupsRasterClose_fndef		cupsRasterClose_fn;

#define	CUPSRASTEROPEN			(*cupsRasterOpen_fn)
#define	CUPSRASTERREADHEADER	(*cupsRasterReadHeader_fn)
#define	CUPSRASTERREADPIXELS	(*cupsRasterReadPixels_fn)
#define	CUPSRASTERCLOSE			(*cupsRasterClose_fn)

typedef	void(*ppdClose_fndef)						(ppd_file_t*ppd);
typedef	ppd_choice_t*(*ppdFindChoice_fndef)			(ppd_option_t*o,constchar*option);
typedef	ppd_choice_t*(*ppdFindMarkedChoice_fndef)	(ppd_file_t*ppd,constchar*keyword);
typedef	ppd_option_t*(*ppdFindOption_fndef)			(ppd_file_t*ppd,constchar*keyword);
typedef	void(*ppdMarkDefaults_fndef)				(ppd_file_t*ppd);
typedef	ppd_file_t*(*ppdOpenFile_fndef)				(constchar*filename);

typedef	void(*cupsFreeOptions_fndef)	(intnum_options,cups_option_t*options);
typedef	int(*cupsParseOptions_fndef)	(constchar*arg,intnum_options,cups_option_t**options);
typedef	int(*cupsMarkOptions_fndef)		(ppd_file_t*ppd,intnum_options,cups_option_t*options);

static	ppdClose_fndefppdClose_fn;
static	ppdFindChoice_fndefppdFindChoice_fn;
static	ppdFindMarkedChoice_fndefppdFindMarkedChoice_fn;
static	ppdFindOption_fndefppdFindOption_fn;
static	ppdMarkDefaults_fndefppdMarkDefaults_fn;
static	ppdOpenFile_fndefppdOpenFile_fn;

static	cupsFreeOptions_fndefcupsFreeOptions_fn;
static	cupsParseOptions_fndefcupsParseOptions_fn;
static	cupsMarkOptions_fndefcupsMarkOptions_fn;

#define	PPDCLOSE			(*ppdClose_fn)
#define	PPDFINDCHOICE		(*ppdFindChoice_fn)
#define	PPDFINDMARKEDCHOICE	(*ppdFindMarkedChoice_fn)
#define	PPDFINDOPTION		(*ppdFindOption_fn)
#define	PPDMARKDEFAULTS 	(*ppdMarkDefaults_fn)
#define	PPDOPENFILE 		(*ppdOpenFile_fn)

#define	CUPSFREEOPTIONS 	(*cupsFreeOptions_fn)
#define	CUPSPARSEOPTIONS	(*cupsParseOptions_fn)
#define	CUPSMARKOPTIONS 	(*cupsMarkOptions_fn)

#else

#define	CUPSRASTEROPEN			cupsRasterOpen
#define	CUPSRASTERREADHEADER	cupsRasterReadHeader
#define	CUPSRASTERREADPIXELS	cupsRasterReadPixels
#define	CUPSRASTERCLOSE			cupsRasterClose

#define	PPDCLOSE				ppdClose
#define	PPDFINDCHOICE			ppdFindChoice
#define	PPDFINDMARKEDCHOICE		ppdFindMarkedChoice
#define	PPDFINDOPTION			ppdFindOption
#define	PPDMARKDEFAULTS			ppdMarkDefaults
#define	PPDOPENFILE				ppdOpenFile

#define	CUPSFREEOPTIONS			cupsFreeOptions
#define	CUPSPARSEOPTIONS		cupsParseOptions
#define	CUPSMARKOPTIONS			cupsMarkOptions

#endif

#define	FALSE 0
#define	TRUE  (!FALSE)

// definitions for printable width
#define	MAX_WIDTH	80
#define	STD_WIDTH	72

struct settings_
{
float	pageWidth;
float	pageHeight;

int		pageCutType;
int		docCutType;

int		cashDrawer;
int		cashDrawerPulseWidth;

int		bytesPerScanLine;
int		bytesPerScanLineStd;
};

struct command
{
int		length;
char*	command;
};

// define printer initialize command
static const struct command printerInitializeCommand=
{2,(char[2]){0x1b,'@'}};

// define start page command
const struct command startPageCommand={2,(char[2]){27,'2'}};

// define end page command
const struct command endPageCommand={0,NULL};

// define end job command
const struct command endJobCommand=	{0,NULL};

// define page cut command
const struct command PageCutCommand={4,(char[]){/*0x1b,'J',176,*/0x1d,'V',66,0}};

inline void outputCommand(struct command output){
int	i;

	for(i=0;i<output.length;i++)	putchar(output.command[i]);
}

inline int getOptionChoiceIndex(const char * choiceName, ppd_file_t * ppd)
{
ppd_choice_t	*choice;
ppd_option_t	*option;

	choice=PPDFINDMARKEDCHOICE(ppd,choiceName);
	if(!choice){
		if(!(option=PPDFINDOPTION(ppd,choiceName)))				return -1;
		if(!(choice=PPDFINDCHOICE(option,option->defchoice)))	return -1;
	}
	return atoi(choice->choice);
}

inline void getPageWidthPageHeight(ppd_file_t * ppd, struct settings_ * settings)
{
ppd_choice_t*	choice;
ppd_option_t*	option;

char			width[20];
char			height[20];
int				Idx=0;
char*			pageSize;


enum{sINIT,sWIDTH,sHEIGHT,sCOMPLETE,sFAIL}	state=sINIT;

	choice=PPDFINDMARKEDCHOICE(ppd,"PageSize");
	if(!choice){
		option=PPDFINDOPTION(ppd,"PageSize");
		choice=PPDFINDCHOICE(option,option->defchoice);
	}

	pageSize=choice->choice;

	while(*pageSize){
		if(state==sINIT){
			if(*pageSize=='X'){
				state=sWIDTH;
				pageSize++;
				continue;
			}
		}else if(state==sWIDTH){
			if ((*pageSize>='0')&&(*pageSize<='9')){
				width[Idx++]=*pageSize++;
				continue;
			}else if(*pageSize=='D'){
				width[Idx++]='.';
				pageSize++;
				continue;
			}else if(*pageSize=='M'){
				pageSize++;
				continue;
			}else if(*pageSize=='Y'){
				state=sHEIGHT;
				width[Idx]=0;
				Idx=0;
				pageSize++;
				continue;
			}
		}else if(state==sHEIGHT){
			if((*pageSize>='0')&&(*pageSize<='9')){
				height[Idx++]=*pageSize++;
				continue;
			}else if(*pageSize=='D'){
				height[Idx++]='.';
				pageSize++;
				continue;
			}else if(*pageSize=='M'){
				height[Idx]=0;
				state=sCOMPLETE;
				break;
			}
		}
		state=sFAIL;
		break;
	}

	if(state==sCOMPLETE){
		settings->pageWidth=atof(width);
		settings->pageHeight=atof(height);
	}else
		settings->pageWidth=
		settings->pageHeight=0;
}

void initializeSettings(char * commandLineOptionSettings, struct settings_ * settings){
ppd_file_t*		ppd;
cups_option_t*	options		=NULL;
int				numOptions;

	ppd=PPDOPENFILE(getenv("PPD"));
	PPDMARKDEFAULTS(ppd);
	numOptions=CUPSPARSEOPTIONS(commandLineOptionSettings,0,&options);
	if((numOptions!=0)&&(options!=NULL)){
		CUPSMARKOPTIONS(ppd,numOptions,options);
		CUPSFREEOPTIONS(numOptions,options);
	}

	memset(settings,0,sizeof(struct settings_));
	settings->pageCutType			=getOptionChoiceIndex("PageCutType"			,ppd);
	settings->docCutType			=getOptionChoiceIndex("DocCutType"			,ppd);
	settings->cashDrawer			=getOptionChoiceIndex("CashDrawer"			,ppd);
	settings->cashDrawerPulseWidth	=getOptionChoiceIndex("CashDrawerPulseWidth",ppd);
	settings->bytesPerScanLine		=MAX_WIDTH;
	settings->bytesPerScanLineStd	=STD_WIDTH;
	getPageWidthPageHeight(ppd,settings);
	PPDCLOSE(ppd);
/*FILE	*f;
	f=fopen(LogFile,"a+t");
	fprintf(f,"pageCutType			%d\n"	\
	          "docCutType			%d\n"	\
	          "cashDrawer			%d\n"	\
	          "cashDrawerPulseWidth	%d\n"	\
	          "bytesPerScanLine		%d\n"	\
	          "bytesPerScanLineStd	%d\n"	,
	          settings->pageCutType			,
	          settings->docCutType			,
	          settings->cashDrawer			,
	          settings->cashDrawerPulseWidth,
	          settings->bytesPerScanLine	,
	          settings->bytesPerScanLineStd	);
	fclose(f);	//*/
}

void jobSetup(struct settings_ settings){
unsigned char	pw;
	outputCommand(printerInitializeCommand);
	if((settings.cashDrawer==1)||(settings.cashDrawer==2)){
		pw=(settings.cashDrawerPulseWidth+1)*25;
		printf("\x1bp%d%c%c",settings.cashDrawer-1,pw,pw);
	}
}

void pageSetup(struct settings_ settings, cups_page_header_t header){
	outputCommand(startPageCommand);
}

void endPage(struct settings_ settings)
{
	outputCommand(endPageCommand);
	if(settings.pageCutType==2)
		outputCommand(PageCutCommand);
}

void endJob(struct settings_ settings)
{
	if(settings.pageCutType==1)
		outputCommand(PageCutCommand);
	outputCommand(endJobCommand);
}

#define	GET_LIB_FN_OR_EXIT_FAILURE(fn_ptr,lib,fn_name)										\
{																							\
	fn_ptr=dlsym(lib,fn_name);																\
	if ((dlerror())){																		\
		fputs("ERROR: required fn not exported from dynamically loaded libary\n",stderr);	\
		if(libCupsImage)	dlclose(libCupsImage);											\
		if(libCups)			dlclose(libCups);												\
		return EXIT_FAILURE;																\
	}																						\
}

#ifdef	RPMBUILD
#define	CLEANUP 			\
{							\
	if(rasterData)			\
		free(rasterData);	\
	CUPSRASTERCLOSE(ras);	\
	if(fd)					\
		close(fd);			\
	dlclose(libCupsImage);	\
	dlclose(libCups);		\
}
#else
#define	CLEANUP 			\
{							\
	if(rasterData)			\
		free(rasterData);	\
	CUPSRASTERCLOSE(ras);	\
	if(fd)					\
		close(fd);			\
}
#endif

int main(int argc, char *argv[])
{
int 				fd;							/* File descriptor providing CUPS raster data											*/
cups_raster_t		*ras; 						/* Raster stream for printing															*/
cups_page_header_t	header; 					/* CUPS Page header 																	*/

int 				y,							/* Vertical position in page 0 <= y <= header.cupsHeight								*/
	 				i,
	 				page=0;						/* Current page 																		*/

unsigned char		*rasterData=NULL;			/* Pointer to raster data buffer														*/
struct settings_	settings;					/* Configuration settings																*/

#ifdef RPMBUILD
void	*libCupsImage;							/* Pointer to libCupsImage library														*/
void	*libCups;								/* Pointer to libCups library															*/
FILE	*f;

	libCups=dlopen("libcups.so",RTLD_NOW|RTLD_GLOBAL);
	if(!libCups){
		fputs("ERROR: libcups.so load failure\n",stderr);
		return EXIT_FAILURE;
	}

	libCupsImage=dlopen("libcupsimage.so",RTLD_NOW|RTLD_GLOBAL);
	if(!libCupsImage){
		fputs("ERROR: libcupsimage.so load failure\n",stderr);
		dlclose(libCups);
		return EXIT_FAILURE;
	}

	GET_LIB_FN_OR_EXIT_FAILURE(ppdClose_fn, 			libCups,	  "ppdClose"			 );
	GET_LIB_FN_OR_EXIT_FAILURE(ppdFindChoice_fn,		libCups,	  "ppdFindChoice"		 );
	GET_LIB_FN_OR_EXIT_FAILURE(ppdFindMarkedChoice_fn,	libCups,	  "ppdFindMarkedChoice"  );
	GET_LIB_FN_OR_EXIT_FAILURE(ppdFindOption_fn,		libCups,	  "ppdFindOption"		 );
	GET_LIB_FN_OR_EXIT_FAILURE(ppdMarkDefaults_fn,		libCups,	  "ppdMarkDefaults" 	 );
	GET_LIB_FN_OR_EXIT_FAILURE(ppdOpenFile_fn,			libCups,	  "ppdOpenFile" 		 );
	GET_LIB_FN_OR_EXIT_FAILURE(cupsFreeOptions_fn,		libCups,	  "cupsFreeOptions" 	 );
	GET_LIB_FN_OR_EXIT_FAILURE(cupsParseOptions_fn, 	libCups,	  "cupsParseOptions"	 );
	GET_LIB_FN_OR_EXIT_FAILURE(cupsMarkOptions_fn,		libCups,	  "cupsMarkOptions" 	 );
	GET_LIB_FN_OR_EXIT_FAILURE(cupsRasterOpen_fn,		libCupsImage, "cupsRasterOpen"		 );
	GET_LIB_FN_OR_EXIT_FAILURE(cupsRasterReadHeader_fn, libCupsImage, "cupsRasterReadHeader" );
	GET_LIB_FN_OR_EXIT_FAILURE(cupsRasterReadPixels_fn, libCupsImage, "cupsRasterReadPixels" );
	GET_LIB_FN_OR_EXIT_FAILURE(cupsRasterClose_fn,		libCupsImage, "cupsRasterClose" 	 );
#endif

	if((argc<6)||(argc>7)){
		fputs("ERROR: rastertocbm1k job-id user title copies options [file]\n",stderr);
		#ifdef RPMBUILD
			dlclose(libCupsImage);
			dlclose(libCups);
		#endif
		return EXIT_FAILURE;
	}

	if(argc==7){
		if((fd=open(argv[6],O_RDONLY))==-1){
			perror("ERROR: Unable to open raster file - ");
			sleep(1);
			#ifdef RPMBUILD
				dlclose(libCupsImage);
				dlclose(libCups);
			#endif
			return EXIT_FAILURE;
		}
	}else
		fd=0;

	initializeSettings(argv[5],&settings);
	jobSetup(settings);

	ras=CUPSRASTEROPEN(fd,CUPS_RASTER_READ);
	while(CUPSRASTERREADHEADER(ras,&header)){
		if((!header.cupsHeight)||(!header.cupsBytesPerLine))	break;
		if(!rasterData){
			if(!(rasterData=malloc(header.cupsBytesPerLine))){
				CLEANUP;
				return EXIT_FAILURE;
			}
		}

		pageSetup(settings,header);
		page++;
		fprintf(stderr,"PAGE: %d %d\n",page,header.NumCopies);

		settings.bytesPerScanLine	=(header.cupsBytesPerLine<=settings.bytesPerScanLine)
									?header.cupsBytesPerLine
									:settings.bytesPerScanLineStd;
		printf("\x1dv00");
		putchar(header.cupsBytesPerLine); putchar(header.cupsBytesPerLine>>8);
		putchar(header.cupsHeight); 	  putchar(header.cupsHeight>>8);

		for(y=0;y<header.cupsHeight;y++){
			if(!(y&127))
				fprintf(stderr,"INFO: Printing page %d, %d%% complete...\n",page,(100*y/header.cupsHeight));
			if(CUPSRASTERREADPIXELS(ras,rasterData,header.cupsBytesPerLine)<1)
				break;
			for(i=0;i<header.cupsBytesPerLine;i++){
				putchar(rasterData[i]);
			}
		}
		endPage(settings);
	}
	endJob(settings);
	CLEANUP;
	fputs(page?"INFO: Ready to print.\n":"ERROR: No pages found!\n",stderr);
	return(page)?EXIT_SUCCESS:EXIT_FAILURE;
}

// end of rastertocbm1k.c
