// hydraharp ht3 reader
// by picoquant, adapted for GNU and additional features by dheera@mit.edu
// documentation to be written ...
#include<stdio.h>
#include<string.h>
#include<stddef.h>
#include<stdlib.h>
#include<inttypes.h>

#define DISPCURVES 8	// not relevant in TT modes but needed in file header definition
#define MAXINPCHANS 8
#define T3WRAPAROUND 1024

#pragma pack(8) //structure alignment to 8 byte boundaries

typedef struct {
  float Start;
  float Step;
  float End;
} tParamStruct;

typedef struct {
  int MapTo;
  int Show;
} tCurveMapping;

typedef	struct {
  int ModelCode;
  int VersionCode;
} tModuleInfo;

typedef union {
  uint32_t allbits;
  struct  {
    unsigned nsync:10; // numer of sync period
    unsigned dtime:15; // delay from last sync in units of chosen resolution 
    unsigned channel:6;
    unsigned special:1;
  } bits;
} tT3Rec;


// The following represents the readable ASCII file header portion. 

struct {
  char Ident[16];				//"HydraHarp"
  char FormatVersion[6];		//file format version
  char CreatorName[18];		//name of creating software
  char CreatorVersion[12];	//version of creating software
  char FileTime[18];
  char CRLF[2];
  char CommentField[256];
} TxtHdr;

// The following is binary file header information indentical to that in HHD files.
// Note that some items are not meaningful in the time tagging modes.

struct {
  int Curves;
  int BitsPerRecord;		// data of one event record has this many bits
  int ActiveCurve;
  int MeasMode;
  int SubMode;
  int Binning;			
  double Resolution;		// in ps
  int Offset;				
  int Tacq;				// in ms
  int StopAt;
  int StopOnOvfl;
  int Restart;
  int DispLinLog;
  int DispTimeFrom;		// 1ns steps
  int DispTimeTo;
  int DispCountsFrom;
  int DispCountsTo;
  tCurveMapping DispCurves[DISPCURVES];	
  tParamStruct Params[3];
  int RepeatMode;
  int RepeatsPerCurve;
  int RepeatTime;
  int RepeatWaitTime;
  char ScriptName[20];
} BinHdr;

// The next is a header carrying hardware information

struct {		
  char HardwareIdent[16];   
  char HardwarePartNo[8]; 
  int  HardwareSerial; 
  int  nModulesPresent;     
  tModuleInfo ModuleInfo[10]; //up to 10 modules can exist
  double BaseResolution;
  unsigned long long int InputsEnabled; // a bitfield; original uses scary double-underscore __int64
  int InpChansPresent;  //this determines the number of ChannelHeaders below!
  int RefClockSource;
  int ExtDevices;     //a bitfield
  int MarkerSettings; //a bitfield
  int SyncDivider;
  int SyncCFDLevel;
  int SyncCFDZeroCross;
  int SyncOffset;
} MainHardwareHdr;

//How many of the following array elements are actually present in the file 
//is indicated by InpChansPresent above. Here we allocate the possible maximum.

struct { 
  int InputModuleIndex;  //in welchem Modul war dieser Kanal
  int InputCFDLevel;
  int InputCFDZeroCross; 
  int InputOffset;
} InputChannelSettings[MAXINPCHANS]; 

//Up to here the header was identical to that of HHD files.
//The following header sections are specific for the TT modes 

//How many of the following array elements are actually present in the file 
//is indicated by InpChansPresent above. Here we allocate the possible maximum.

int InputRate[MAXINPCHANS];

//the following exists only once				

struct {	
  int SyncRate;
  int StopAfter;
  int StopReason;
  int ImgHdrSize;
  unsigned long long int nRecords; // underscore-free, __int64 is scary
} TTTRHdr;

//how many of the following ImgHdr array elements are actually present in the file 
//is indicated by ImgHdrSize above. 
//Storage must be allocated dynamically if ImgHdrSize other than 0 is found.
//				int ImgHdr[ImgHdrSize];

//The headers end after ImgHdr. Following in the file are only event records. 
//How many of them actually are in the file is indicated by nRecords in TTTRHdr above.

int main(int argc, char* argv[]) {
  int result;
  FILE* fpin;
  FILE* fpout_coinc; 	
  FILE* fpout_ch0;	
  FILE* fpout_ch1;
  FILE* fpout_info;
  int i;
  tT3Rec T3Rec;
  unsigned long long int lastmarker[20]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  unsigned long long int n, truensync=0, oflcorrection = 0; // more readable than scary-looking double-underscores "__int64"

  unsigned long long int marker_total=0;
  unsigned long long int dtime_total=0;

  char infile[512];
  char* pch;
  char outfile_coinc[512];
  char outfile_ch0[512];
  char outfile_ch1[512];
  char outfile_info[512];

  unsigned long long int last_signal_nsync=-1;
  unsigned long long int last_idler_nsync=-1;
  unsigned long long int last_signal_dtime=-1;
  unsigned long long int last_idler_dtime=-1;

  unsigned long long int total_signal=0;
  unsigned long long int total_idler=0;
  unsigned long long int total_coincidences=0;

  if(argc<2) {
    fprintf(stderr,"usage: ht3coinc infile.ht3\n");
    exit(-1);
  }

  strncpy(infile,argv[argc-1],sizeof(outfile_coinc));

  strncpy(outfile_info,argv[argc-1],sizeof(outfile_info));
  pch=strstr(outfile_info,".ht3");
  strcpy(pch,".info.txt");

  strncpy(outfile_coinc,argv[argc-1],sizeof(outfile_coinc));
  pch=strstr(outfile_coinc,".ht3");
  strcpy(pch,".coinc.out");

  strncpy(outfile_ch0,argv[argc-1],sizeof(outfile_ch0));
  pch=strstr(outfile_ch0,".ht3");
  strcpy(pch,".ch0.out");

  strncpy(outfile_ch1,argv[argc-1],sizeof(outfile_ch1));
  pch=strstr(outfile_ch1,".ht3");
  strcpy(pch,".ch1.out");
  

  if((fpin=fopen(infile,"rb"))==NULL) {
    fprintf(stderr,"error: input file %s cannot be opened, aborting.\n",infile);
    exit(1);
  }

  if((fpout_info=fopen(outfile_info,"w"))==NULL) {
    fprintf(stderr,"error: output file %s cannot be opened, aborting.\n",outfile_info);
    exit(1);
  }
	
  if((fpout_coinc=fopen(outfile_coinc,"w"))==NULL) {
    fprintf(stderr,"error: output file %s cannot be opened, aborting.\n",outfile_coinc);
    exit(1);
  }

  if((fpout_ch0=fopen(outfile_ch0,"w"))==NULL) {
    fprintf(stderr,"error: output file %s cannot be opened, aborting.\n",outfile_ch0);
    exit(1);
  }

  if((fpout_ch1=fopen(outfile_ch1,"w"))==NULL) {
    fprintf(stderr,"error: output file %s cannot be opened, aborting.\n",outfile_ch1);
    exit(1);
  }

  result = fread( &TxtHdr, 1, sizeof(TxtHdr) ,fpin);
  if(result!=sizeof(TxtHdr)) {
    fprintf(stderr,"error: cannot read text header\n");
    exit(1);
  }
    fprintf(fpout_info,"%% Ident             : %.*s\n",(int)sizeof(TxtHdr.Ident),TxtHdr.Ident);
    fprintf(fpout_info,"%% Format Version    : %.*s\n",(int)sizeof(TxtHdr.FormatVersion),TxtHdr.FormatVersion);
    fprintf(fpout_info,"%% Creator Name      : %.*s\n",(int)sizeof(TxtHdr.CreatorName),TxtHdr.CreatorName);
    fprintf(fpout_info,"%% Creator Version   : %.*s\n",(int)sizeof(TxtHdr.CreatorVersion),TxtHdr.CreatorVersion);
    fprintf(fpout_info,"%% Time of Creation  : %.*s\n",(int)sizeof(TxtHdr.FileTime),TxtHdr.FileTime);
    fprintf(fpout_info,"%% File Comment      : %.*s\n",(int)sizeof(TxtHdr.CommentField),TxtHdr.CommentField);

  if(strncmp(TxtHdr.FormatVersion,"1.0",3)&&strncmp(TxtHdr.FormatVersion,"2.0",3)) {
    fprintf(stderr,"error: File format version is %s. This program is for version 1.0 and 2.0 only.\n", TxtHdr.FormatVersion);
    exit(1);
  }

  result = fread( &BinHdr, 1, sizeof(BinHdr) ,fpin);
  if(result!=sizeof(BinHdr)) {
    fprintf(stderr,"error: cannot read bin header, aborted.\n");
    exit(1);
  }
    fprintf(fpout_info,"%% Bits per Record   : %d\n",BinHdr.BitsPerRecord);
    fprintf(fpout_info,"%% Measurement Mode  : %d\n",BinHdr.MeasMode);
    fprintf(fpout_info,"%% Sub-Mode          : %d\n",BinHdr.SubMode);
    fprintf(fpout_info,"%% Binning           : %d\n",BinHdr.Binning);
    fprintf(fpout_info,"%% Resolution        : %lf\n",BinHdr.Resolution);
    fprintf(fpout_info,"%% Offset            : %d\n",BinHdr.Offset);
    fprintf(fpout_info,"%% AcquisitionTime   : %d\n",BinHdr.Tacq);
  // Note: for formal reasons the BinHdr is identical to that of HHD files.  
  // It therefore contains some settings that are not relevant in the TT modes,
  // e.g. the curve display settings. So we do not write them out here.

  result = fread( &MainHardwareHdr, 1, sizeof(MainHardwareHdr) ,fpin);
  if(result!=sizeof(MainHardwareHdr)) {
    fprintf(stderr,"error: cannot read MainHardwareHdr, aborted.\n");
    exit(1);
  }

    fprintf(fpout_info,"%% HardwareIdent     : %.*s\n",(int)sizeof(MainHardwareHdr.HardwareIdent),MainHardwareHdr.HardwareIdent);
    fprintf(fpout_info,"%% HardwarePartNo    : %.*s\n",(int)sizeof(MainHardwareHdr.HardwarePartNo),MainHardwareHdr.HardwarePartNo);
    fprintf(fpout_info,"%% HardwareSerial    : %d\n",MainHardwareHdr.HardwareSerial);
    fprintf(fpout_info,"%% nModulesPresent   : %d\n",MainHardwareHdr.nModulesPresent);


  // the following module info is needed for support enquiries only
  for(i=0;i<MainHardwareHdr.nModulesPresent;++i) {
      fprintf(fpout_info,"%% Moduleinfo[%02d]    : %08x %08x\n",
        i, MainHardwareHdr.ModuleInfo[i].ModelCode, MainHardwareHdr.ModuleInfo[i].VersionCode);
  }

  //the following are important measurement settings
    fprintf(fpout_info,"%% BaseResolution    : %lf\n",MainHardwareHdr.BaseResolution);
    fprintf(fpout_info,"%% InputsEnabled     : %llu\n",MainHardwareHdr.InputsEnabled);  //actually a bitfield
    fprintf(fpout_info,"%% InpChansPresent   : %d\n",MainHardwareHdr.InpChansPresent);
    fprintf(fpout_info,"%% RefClockSource    : %d\n",MainHardwareHdr.RefClockSource);
    fprintf(fpout_info,"%% ExtDevices        : %x\n",MainHardwareHdr.ExtDevices);     //actually a bitfield	
    fprintf(fpout_info,"%% MarkerSettings    : %x\n",MainHardwareHdr.MarkerSettings); //actually a bitfield
    fprintf(fpout_info,"%% SyncDivider       : %d\n",MainHardwareHdr.SyncDivider);
    fprintf(fpout_info,"%% SyncCFDLevel      : %d\n",MainHardwareHdr.SyncCFDLevel);
    fprintf(fpout_info,"%% SyncCFDZeroCross  : %d\n",MainHardwareHdr.SyncCFDZeroCross);
    fprintf(fpout_info,"%% SyncOffset        : %d\n",MainHardwareHdr.SyncOffset);


  for(i=0;i<MainHardwareHdr.InpChansPresent;++i) {
      fprintf(fpout_info,"%% ---------------------\n");
      result = fread( &(InputChannelSettings[i]), 1, sizeof(InputChannelSettings[i]) ,fpin);
      if(result!=sizeof(InputChannelSettings[i])) {
        printf("\nerror reading InputChannelSettings, aborted.");
        exit(1);
      }
      fprintf(fpout_info,"%% Input Channel %1d\n",i);
      fprintf(fpout_info,"%%  InputModuleIndex  : %d\n",InputChannelSettings[i].InputModuleIndex);
      fprintf(fpout_info,"%%  InputCFDLevel     : %d\n",InputChannelSettings[i].InputCFDLevel);
      fprintf(fpout_info,"%%  InputCFDZeroCross : %d\n",InputChannelSettings[i].InputCFDZeroCross);
      fprintf(fpout_info,"%%  InputOffset       : %d\n",InputChannelSettings[i].InputOffset);
  }

    fprintf(fpout_info,"%% ---------------------\n");
  for(i=0;i<MainHardwareHdr.InpChansPresent;++i) {
    result = fread( &(InputRate[i]), 1, sizeof(InputRate[i]) ,fpin);
    if(result!=sizeof(InputRate[i])) {
      fprintf(stderr,"error reading InputRates, aborted\n");
      exit(1);
    }
      fprintf(fpout_info,"%% Input Rate [%1d]     : %1d\n",i,InputRate[i]);
  }

    fprintf(fpout_info,"%% ---------------------\n");

  result = fread( &TTTRHdr, 1, sizeof(TTTRHdr) ,fpin);
  if(result!=sizeof(TTTRHdr)) {
    fprintf(stderr,"error: error reading TTTRHdr, aborted\n");
    exit(1);
  }

    fprintf(fpout_info,"%% SyncRate           : %d\n",TTTRHdr.SyncRate);
    fprintf(fpout_info,"%% StopAfter          : %d\n",TTTRHdr.StopAfter);
    fprintf(fpout_info,"%% StopReason         : %d\n",TTTRHdr.StopReason);
    fprintf(fpout_info,"%% ImgHdrSize         : %d\n",TTTRHdr.ImgHdrSize);
    fprintf(fpout_info,"%% nRecords           : %llu\n",TTTRHdr.nRecords);
    fprintf(fpout_info,"%% ---------------------\n");

  //For this simple demo we skip the imaging header.
  //You will need to read it if you want to interpret an imaging file.
  fseek(fpin,TTTRHdr.ImgHdrSize*4,SEEK_CUR);


  //now read and interpret the event records
  for(n=0;n<TTTRHdr.nRecords;n++) {
    i=0;  
    result = fread(&T3Rec.allbits,sizeof(T3Rec.allbits),1,fpin); 
    if(result!=1) {
      if(feof(fpin)==0) {
        fprintf(stderr,"error: error in input file\n");
        exit(1);
      }
    }

    if(T3Rec.bits.special==1) {
      if(T3Rec.bits.channel==0x3F) { // overflow
        if(T3Rec.bits.nsync==0)
          oflcorrection+=T3WRAPAROUND;
        else
          oflcorrection+=T3WRAPAROUND*T3Rec.bits.nsync;
      }		
      if((T3Rec.bits.channel>=1)&&(T3Rec.bits.channel<=15)) { // marker
        truensync = oflcorrection + T3Rec.bits.nsync;
        // the time unit depends on sync period which can be obtained from the file header
      }
    } else { // regular input channel
      truensync = oflcorrection + T3Rec.bits.nsync; 
      // the nsync time unit depends on sync period which can be obtained from the file header
      // the dtime unit depends on the resolution and can also be obtained from the file header
      // fprintf(fpout,"%llu 1 %02x %llu %u\n", n, T3Rec.bits.channel, truensync, T3Rec.bits.dtime);
      if(T3Rec.bits.channel==0) {
        fprintf(fpout_ch0,"%llu %d\n", truensync, T3Rec.bits.dtime);
        total_signal++;
        last_signal_nsync=truensync;
        last_signal_dtime=T3Rec.bits.dtime;
        if(last_signal_nsync-last_idler_nsync<=1) {
          fprintf(fpout_coinc,"%llu %llu %llu %llu\n", last_signal_nsync, last_idler_nsync, last_signal_dtime, last_idler_dtime);
          total_coincidences++;
        }
      }
      if(T3Rec.bits.channel==1) {
        fprintf(fpout_ch1,"%llu %d\n", truensync, T3Rec.bits.dtime);
        total_idler++;
        last_idler_nsync=truensync;
        last_idler_dtime=T3Rec.bits.dtime;
        if(last_idler_nsync-last_signal_nsync<=1) {
          fprintf(fpout_coinc,"%llu %llu %llu %llu\n", last_signal_nsync, last_idler_nsync, last_signal_dtime, last_idler_dtime);
          total_coincidences++;
        }
      }
    }
    if(n%100000==0) {
      fprintf(stdout,"[%llu%%] Signal %llu / Idler %llu / Coinc %llu\r",100*n/TTTRHdr.nRecords,total_signal,total_idler,total_coincidences);
      fflush(stdout);
    }
  }
  fprintf(stdout,"[%llu%%] Signal %llu / Idler %llu / Coinc %llu\n\n",100*n/TTTRHdr.nRecords,total_signal,total_idler,total_coincidences);

  fclose(fpin);
  fclose(fpout_coinc); 
  fclose(fpout_ch0); 
  fclose(fpout_ch1); 
  fclose(fpout_info); 

  exit(0);
  return(0);		
}
