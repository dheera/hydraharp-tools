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
  FILE* fpout; 	
  int i;
  tT3Rec T3Rec;
  unsigned long long int lastmarker[20]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  unsigned long long int n, truensync=0, oflcorrection = 0; // more readable than scary-looking double-underscores "__int64"

  unsigned long long int marker_total=0;
  unsigned long long int dtime_total=0;

  int print_headers=0; // print headers or not
  char infile[512];
  char outfile[512];

  unsigned long long int last_signal_nsync=-1;
  unsigned long long int last_idler_nsync=-1;
  unsigned long long int last_signal_dtime=-1;
  unsigned long long int last_idler_dtime=-1;

  unsigned long long int total_signal=0;
  unsigned long long int total_idler=0;
  unsigned long long int total_coincidences=0;

  if(argc<3) {
    fprintf(stderr,"usage: ht3coinc infile.ht3 oufile.txt\n");
    exit(-1);
  }

  if((fpin=fopen(argv[argc-2],"rb"))==NULL) {
    fprintf(stderr,"error: input file cannot be opened, aborting.\n");
    exit(1);
  }
	
  if((fpout=fopen(argv[argc-1],"w"))==NULL) {
    fprintf(stderr,"error: output file cannot be opened, aborting.\n");
    exit(1);
  }

  fprintf(stdout,"Loading data from %s ...\n", argv[argc-2]);
  fprintf(stdout,"Writing output to %s ...\n", argv[argc-1]);
  result = fread( &TxtHdr, 1, sizeof(TxtHdr) ,fpin);
  if(result!=sizeof(TxtHdr)) {
    fprintf(stderr,"error: cannot read text header\n");
    exit(1);
  }
  if(print_headers) {
    fprintf(fpout,"%% Ident             : %.*s\n",(int)sizeof(TxtHdr.Ident),TxtHdr.Ident);
    fprintf(fpout,"%% Format Version    : %.*s\n",(int)sizeof(TxtHdr.FormatVersion),TxtHdr.FormatVersion);
    fprintf(fpout,"%% Creator Name      : %.*s\n",(int)sizeof(TxtHdr.CreatorName),TxtHdr.CreatorName);
    fprintf(fpout,"%% Creator Version   : %.*s\n",(int)sizeof(TxtHdr.CreatorVersion),TxtHdr.CreatorVersion);
    fprintf(fpout,"%% Time of Creation  : %.*s\n",(int)sizeof(TxtHdr.FileTime),TxtHdr.FileTime);
    fprintf(fpout,"%% File Comment      : %.*s\n",(int)sizeof(TxtHdr.CommentField),TxtHdr.CommentField);
  }

  if(strncmp(TxtHdr.FormatVersion,"1.0",3)&&strncmp(TxtHdr.FormatVersion,"2.0",3)) {
    if(print_headers) {
      fprintf(stderr,"error: File format version is %s. This program is for version 1.0 and 2.0 only.\n", TxtHdr.FormatVersion);
    }
    exit(1);
  }

  result = fread( &BinHdr, 1, sizeof(BinHdr) ,fpin);
  if(result!=sizeof(BinHdr)) {
    fprintf(stderr,"error: cannot read bin header, aborted.\n");
    exit(1);
  }
  if(print_headers) {
    fprintf(fpout,"%% Bits per Record   : %d\n",BinHdr.BitsPerRecord);
    fprintf(fpout,"%% Measurement Mode  : %d\n",BinHdr.MeasMode);
    fprintf(fpout,"%% Sub-Mode          : %d\n",BinHdr.SubMode);
    fprintf(fpout,"%% Binning           : %d\n",BinHdr.Binning);
    fprintf(fpout,"%% Resolution        : %lf\n",BinHdr.Resolution);
    fprintf(fpout,"%% Offset            : %d\n",BinHdr.Offset);
    fprintf(fpout,"%% AcquisitionTime   : %d\n",BinHdr.Tacq);
  }
  // Note: for formal reasons the BinHdr is identical to that of HHD files.  
  // It therefore contains some settings that are not relevant in the TT modes,
  // e.g. the curve display settings. So we do not write them out here.

  result = fread( &MainHardwareHdr, 1, sizeof(MainHardwareHdr) ,fpin);
  if(result!=sizeof(MainHardwareHdr)) {
    fprintf(stderr,"error: cannot read MainHardwareHdr, aborted.\n");
    exit(1);
  }

  if(print_headers) {
    fprintf(fpout,"%% HardwareIdent     : %.*s\n",(int)sizeof(MainHardwareHdr.HardwareIdent),MainHardwareHdr.HardwareIdent);
    fprintf(fpout,"%% HardwarePartNo    : %.*s\n",(int)sizeof(MainHardwareHdr.HardwarePartNo),MainHardwareHdr.HardwarePartNo);
    fprintf(fpout,"%% HardwareSerial    : %d\n",MainHardwareHdr.HardwareSerial);
    fprintf(fpout,"%% nModulesPresent   : %d\n",MainHardwareHdr.nModulesPresent);
  }


  // the following module info is needed for support enquiries only
  for(i=0;i<MainHardwareHdr.nModulesPresent;++i) {
    if(print_headers) {
      fprintf(fpout,"%% Moduleinfo[%02d]    : %08x %08x\n",
        i, MainHardwareHdr.ModuleInfo[i].ModelCode, MainHardwareHdr.ModuleInfo[i].VersionCode);
    }
  }

  //the following are important measurement settings
  if(print_headers) {
    fprintf(fpout,"%% BaseResolution    : %lf\n",MainHardwareHdr.BaseResolution);
    fprintf(fpout,"%% InputsEnabled     : %llu\n",MainHardwareHdr.InputsEnabled);  //actually a bitfield
    fprintf(fpout,"%% InpChansPresent   : %d\n",MainHardwareHdr.InpChansPresent);
    fprintf(fpout,"%% RefClockSource    : %d\n",MainHardwareHdr.RefClockSource);
    fprintf(fpout,"%% ExtDevices        : %x\n",MainHardwareHdr.ExtDevices);     //actually a bitfield	
    fprintf(fpout,"%% MarkerSettings    : %x\n",MainHardwareHdr.MarkerSettings); //actually a bitfield
    fprintf(fpout,"%% SyncDivider       : %d\n",MainHardwareHdr.SyncDivider);
    fprintf(fpout,"%% SyncCFDLevel      : %d\n",MainHardwareHdr.SyncCFDLevel);
    fprintf(fpout,"%% SyncCFDZeroCross  : %d\n",MainHardwareHdr.SyncCFDZeroCross);
    fprintf(fpout,"%% SyncOffset        : %d\n",MainHardwareHdr.SyncOffset);
  }


  for(i=0;i<MainHardwareHdr.InpChansPresent;++i) {
    if(print_headers) {
      fprintf(fpout,"%% ---------------------\n");
    }
      result = fread( &(InputChannelSettings[i]), 1, sizeof(InputChannelSettings[i]) ,fpin);
      if(result!=sizeof(InputChannelSettings[i])) {
        printf("\nerror reading InputChannelSettings, aborted.");
        exit(1);
      }
    if(print_headers) {
      fprintf(fpout,"%% Input Channel %1d\n",i);
      fprintf(fpout,"%%  InputModuleIndex  : %d\n",InputChannelSettings[i].InputModuleIndex);
      fprintf(fpout,"%%  InputCFDLevel     : %d\n",InputChannelSettings[i].InputCFDLevel);
      fprintf(fpout,"%%  InputCFDZeroCross : %d\n",InputChannelSettings[i].InputCFDZeroCross);
      fprintf(fpout,"%%  InputOffset       : %d\n",InputChannelSettings[i].InputOffset);
    }
  }

  if(print_headers) {
    fprintf(fpout,"%% ---------------------\n");
  }
  for(i=0;i<MainHardwareHdr.InpChansPresent;++i) {
    result = fread( &(InputRate[i]), 1, sizeof(InputRate[i]) ,fpin);
    if(result!=sizeof(InputRate[i])) {
      fprintf(stderr,"error reading InputRates, aborted\n");
      exit(1);
    }
    if(print_headers) {
      fprintf(fpout,"%% Input Rate [%1d]     : %1d\n",i,InputRate[i]);
    }
  }

  if(print_headers) {
    fprintf(fpout,"%% ---------------------\n");
  }

  result = fread( &TTTRHdr, 1, sizeof(TTTRHdr) ,fpin);
  if(result!=sizeof(TTTRHdr)) {
    fprintf(stderr,"error: error reading TTTRHdr, aborted\n");
    exit(1);
  }

  if(print_headers) {
    fprintf(fpout,"%% SyncRate           : %d\n",TTTRHdr.SyncRate);
    fprintf(fpout,"%% StopAfter          : %d\n",TTTRHdr.StopAfter);
    fprintf(fpout,"%% StopReason         : %d\n",TTTRHdr.StopReason);
    fprintf(fpout,"%% ImgHdrSize         : %d\n",TTTRHdr.ImgHdrSize);
    fprintf(fpout,"%% nRecords           : %llu\n",TTTRHdr.nRecords);
    fprintf(fpout,"%% ---------------------\n");
  }

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
        total_signal++;
        last_signal_nsync=truensync;
        last_signal_dtime=T3Rec.bits.dtime;
        if(last_signal_nsync-last_idler_nsync<=1) {
          fprintf(fpout,"%llu %llu %llu %llu\n", last_signal_nsync, last_idler_nsync, last_signal_dtime, last_idler_dtime);
          total_coincidences++;
        }
      }
      if(T3Rec.bits.channel==1) {
        total_idler++;
        last_idler_nsync=truensync;
        last_idler_dtime=T3Rec.bits.dtime;
        if(last_idler_nsync-last_signal_nsync<=1) {
          fprintf(fpout,"%llu %llu %llu %llu\n", last_signal_nsync, last_idler_nsync, last_signal_dtime, last_idler_dtime);
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
  fclose(fpout); 

  exit(0);
  return(0);		
}
