/*llopen*/

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>

#define BAUDRATE 9600 //38400?
#define MODEMDEVICE "/dev/ttyS1"
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1
#define MAXR 3 //no maximo de falhas
#define MAXT 1 //valor do temporizador
#define MAX_SIZE 256 //Tamanho maximo de uma frame APÓS stuffing

#define FLAG 0x7E
#define ESC 0x7D
#define STFF 0x20
#define SET 0x03
#define DISC 0x0B
#define UA 0x07 
#define RR 0x05
#define REJ 0x01
#define A 0x03 //Comando enviado pelo emissor e resposta enviada pelo receptor

#define RECEIVER 0
#define TRANSMITTER 1

#define DATA_RECEIVED 0
#define SET_RECEIVED 1
#define DISC_RECEIVED 2
#define UA_RECEIVED 3
#define RR_RECEIVED 4
#define REJ_RECEIVED 5

int fd, txrx, N;

volatile int STOP=FALSE;
int conta = 0;
int alarm_flag = 1;

struct linkLayerStruct{
	char port[20];
	int fd;
	unsigned int sequenceNumber;
	unsigned int timeout;//valor do temporizador
	int frameLength;
	char frame[MAX_SIZE];
	unsigned int numTransmissions;
	//int maxFrameSize;
	//int status;
}linkLayer;


void atende();

unsigned int byte_stuffing(unsigned char *data, unsigned char *stuffed_data, int length);

int sendSupervisionFrame(int fd, unsigned char C);

int sendInformationFrame(unsigned char * data, int length);

int receiveframe(char *data, int * length);

int llopen(int fd, int txrx);

int llread(int fd, char* buffer);

int llwrite(int fd, unsigned char* buffer, int length);

int llclose(int fd, int txrx);

int main (int argc, char** argv) ;
