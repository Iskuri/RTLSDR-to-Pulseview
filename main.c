#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <math.h>
#include "rtl-sdr.h"
#include "zip.h"

rtlsdr_dev_t* dev = NULL;

unsigned char* buffData;
uint32_t buffInc = 0;
#define BUFF_SIZE 16*1024*1024

uint8_t done = 0;

void buffCallback(unsigned char *buf, uint32_t len, void *ctx) {

	memcpy(&buffData[buffInc],buf,len);

	buffInc += len;

	if(buffInc >= BUFF_SIZE) {
		rtlsdr_cancel_async(dev);
		done = 1;
	}

}

int writeString(int f, char* string) {

	return write(f,string,strlen(string));
}

void generateMetaFile(float sampleRate) {

	int f = open("metadata",O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);

	writeString(f,"[global]\n");
	writeString(f,"sigrok version=0.6.0-git-86a1571\n\n");

	writeString(f,"[device 1]\n");
	char sampleRateStr[128];
	sprintf(sampleRateStr,"samplerate=%f MHz\n",sampleRate/1e6*2);
	writeString(f,sampleRateStr);
	// writeString(f,"total probes=0\n");
	writeString(f,"total analog=3\n");
	// writeString(f,"total analog=1\n");

	// probe names
	writeString(f,"analog1=I\n");
	writeString(f,"analog2=Q\n");
	writeString(f,"analog3=AM\n");
	// writeString(f,"analog4=FM\n");
	writeString(f,"unitsize=1\n");

	close(f);
}

void generateVersionFile() {

	int f = open("version",O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);

	write(f,"2",1);
	close(f);
}

uint16_t squares[256];
int abs8(int x) {
	if (x >= 127) {
		return x - 127;
	}
	return 127 - x;
}
void computeSquares() {
	int i, j;
	for (i=0; i<256; i++) {
		j = abs8(i);
		squares[i] = (uint16_t)(j*j);
	}
}

void generateFMFiles() {

	int inc = 0;

	int fmFile = open("analog-1-4-1",O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);

	unsigned char fmBuff[4096];

	double lastPhase = 0;
	double largestFreq = 0;

	while(inc < BUFF_SIZE) {

		double phase = atan2(buffData[inc+1],buffData[inc]);

		double lastFreq = phase-lastPhase;

		if(lastFreq < 0) {
			lastFreq = -lastFreq;
		}

		if(lastFreq > largestFreq) {
			largestFreq = lastFreq;
		}

		lastPhase = phase;
		inc += 2;
	}

	inc = 0;
	while(inc < BUFF_SIZE) {
	
		for(int j = 0 ; j < (4096/4) ; j++) {

			double phase = atan2(buffData[inc+1],buffData[inc]);

			double lastFreq = phase-lastPhase;

			float fmVal = lastFreq * 20 / largestFreq;

			memcpy(&fmBuff[j*4],&fmVal,4);

			inc += 2;
		}
		write(fmFile,fmBuff,4096);
	}

	close(fmFile);
}

void generateAMFiles() {

	computeSquares();

	int amFile = open("analog-1-3-1",O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);

	unsigned char amBuff[4096];

	uint32_t inc = 0;

	float largestVal = 0;
	// get max buffer size
	while(inc < BUFF_SIZE) {
		float amVal = squares[buffData[inc]] + squares[buffData[inc+1]];
		
		if(amVal > largestVal) {
			largestVal = amVal;
		}

		inc+=2;
	}

	inc = 0;
	while(inc < BUFF_SIZE) {
	
		for(int j = 0 ; j < (4096/4) ; j++) {

			float amVal = squares[buffData[inc]] + squares[buffData[inc+1]];

			amVal = amVal * 20 / largestVal;
			// printf("AM Val: %f\n",amVal);

			memcpy(&amBuff[j*4],&amVal,4);

			inc += 2;
		}
		write(amFile,amBuff,4096);
	}

	close(amFile);
}

void generateIQFiles() {

	// analog file uses floats -10 -> 10

	int iFile = open("analog-1-1-1",O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
	int qFile = open("analog-1-2-1",O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);

	uint8_t iBuff[4096];
	uint8_t qBuff[4096];

	float fl = -20;

	uint32_t inc = 0;

	float iLargestVal = 0;
	float qLargestVal = 0;

	while(inc < BUFF_SIZE) {

		int8_t iVal = buffData[inc]-128;
		int8_t qVal = buffData[inc+1]-128;

		if(iVal < 0) {
			iVal = -iVal;
		}

		if(qVal < 0) {
			qVal = -qVal;
		}

		if(iVal > iLargestVal) {
			iLargestVal = iVal;
		}
		
		if(qVal > qLargestVal) {
			qLargestVal = qVal;
		}

		inc += 2;
	}

	inc = 0;

	while(inc < BUFF_SIZE) {

		for(int j = 0 ; j < (4096/4) ; j++) {

			int8_t iVal = buffData[inc]-128;
			int8_t qVal = buffData[inc+1]-128;

			float iFloat = iVal;
			float qFloat = qVal;

			iFloat = iFloat * 20 / iLargestVal;
			qFloat = qFloat * 20 / qLargestVal;

			memcpy(&iBuff[j*4],&iFloat,4);
			memcpy(&qBuff[j*4],&qFloat,4);

			// printf("IQ: %f  %f %04x %04x\n",iFloat,qFloat,buffData[inc],buffData[inc+1]);

			inc += 2;
		}

		write(iFile,iBuff,4096);
		write(qFile,qBuff,4096);


	}

	close(iFile);
	close(qFile);

}

void usage(char* executable) {

	printf("Usage: %s frequency [sample rate]\n",executable);
	exit(1);
}

char filesToZip[][64] = {
	"metadata",
	"version",
	"analog-1-1-1",
	"analog-1-2-1",
	"analog-1-3-1"
};

int main(int argc, char** argv) {

	// check input values
	if(argc < 2) {

		usage(argv[0]);
	}

	if(atof(argv[1]) == 0) {
		usage(argv[0]);
	}


	if(argc >= 3 && atof(argv[2]) == 0) {
		usage(argv[0]);
	}

	float sampleRate = 2e6;

	if(argc >= 3) {
		sampleRate = atof(argv[2]);
	}

	// get some rtlsdr data
	int ret = rtlsdr_open(&dev, 0);

	if(ret < 0) {
		printf("RTLSDR Open failed\n");
		return 1;
	}

	buffData = malloc(BUFF_SIZE);

	int gainCount;
	int allGains[100];

	// get highest gain
	gainCount = rtlsdr_get_tuner_gains(dev, allGains);
	rtlsdr_set_tuner_gain(dev, allGains[gainCount-1]);

	rtlsdr_set_center_freq(dev, atof(argv[1]));

	rtlsdr_set_sample_rate(dev, sampleRate);

    rtlsdr_reset_buffer(dev);

	rtlsdr_read_async(dev, buffCallback, NULL,
		12,
		16384
	);

	while(done == 0);

	printf("Generating files\n");

	// generate metadata files
	generateVersionFile();
	generateMetaFile(sampleRate);

	// Generate wave files
	generateIQFiles();
	generateAMFiles();
	// generateFMFiles();

	// merge into zip file
	printf("Generating srzip\n");

	char srName[128];
	sprintf(srName,"%s_%d.sr",argv[1],time(NULL));

	int error = 0;
	zip_t* archive = zip_open(srName, ZIP_CREATE, &error);

	for(int i = 0 ; i < 5 ; i++) {
		zip_source_t* source = zip_source_file(archive, filesToZip[i],0,0);
		zip_file_add(archive, filesToZip[i],source,ZIP_FL_OVERWRITE);
	}

	zip_close(archive);

	for(int i = 0 ; i < 5 ; i++) {
		unlink(filesToZip[i]);	
	}

	free(buffData);

	printf("Sigrok file saved to: %s\n",srName);

	return 0;
}
