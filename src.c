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
#define MAX_SIZE 512 //Tamanho maximo de uma frame APÓS stuffing

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


void atende() {
	printf("alarme # %d\n", conta);
	alarm_flag = 1;
	linkLayer.numTransmissions++;
}


unsigned int byte_stuffing(unsigned char *data, unsigned char *stuffed_data, int length) {
	printf("[stuffing] START\n");
	unsigned int i;
	unsigned int j = 0;	
	int lengthAfterStuffing = length;
	for(i = 0; i < length; i++) {
		if (data[i] == FLAG || data[i] == ESC) { //Fazemos stuffing em 2 situações: detecção FLAG ou do byte ESC
			stuffed_data[j] = ESC;
			stuffed_data[j+1] = (STFF ^ data[i]); //STFF corresponde a 0x20. Este valor vale 0x5E ou 0x5D conforme a situação
			j++;
			lengthAfterStuffing++; 
		}
		else{
			stuffed_data[j] = data[i];
		}
		j++;	
	}


	return lengthAfterStuffing;
}


int sendSupervisionFrame(int fd, unsigned char C) {
	printf("[sendSup] START\n");
	int n_bytes = 0;
	//unsigned char bytes[5];
	//Este vetor de bytes corresponde à frame a ser enviada. Podiamos também usar a variável frame?
	
	unsigned char BBC1;
	
	if(linkLayer.sequenceNumber == 1){ //se sequenceNumber for 1 o receptor "pede-nos" uma trama do tipo 1
		if(C == RR){
			C = RR | (1 << 7);
		}else if(C == REJ){
			C = REJ | (1 << 7);
		}
	}	
	
	//printf("Send supervision 0x%x", C);
	
	BBC1 = A ^ C;
		
	linkLayer.frame[0] = FLAG;
	linkLayer.frame[1] = A;
	linkLayer.frame[2] = C;
	linkLayer.frame[3] = BBC1; 
	linkLayer.frame[4] = FLAG;
	//Possivelmente?
	//linkLayer.frame[5] = 0; 
	
	n_bytes = write(fd, linkLayer.frame, 5); //6?
	printf("[sendSup] Bytes written: %d\n", n_bytes);
	return n_bytes;
}

int sendInformationFrame(unsigned char * data, int length){
	printf("[sendinf] START\n");
	unsigned char stuffed_data[MAX_SIZE-6];
	
	if(length > MAX_SIZE/2){
		printf("That size is too big!");
		return -1;
	}
	
	int C = linkLayer.sequenceNumber << 6;
	int BCC1 = A ^ C;
	int i;
	
	//Se não funcionar, tentar usar um buffer auxiliar para onde vai
	//a data antes de fazer estas operações
		
	//De forma a calcular BBC2 simplesmente fazemos o calculo do XOR
	//de cada byte de data com o proximo e desse resultado com o proximo.
	//O cálculo de BBC2 é realizado antes do byte stuffing
	int BCC2 = data[0];
	
	for(i = 1; i < length; i++){
		BCC2 = (BCC2 ^ data[i]);
	}
	
	unsigned int lengthAfterStuffing = byte_stuffing(data, stuffed_data, length);
	
	linkLayer.frame[0] = FLAG;
	linkLayer.frame[1] = A;
	linkLayer.frame[2] = C;
	linkLayer.frame[3] = BCC1;
	memcpy(linkLayer.frame+4, stuffed_data, lengthAfterStuffing); //Ou linkLayer.frame+4
	linkLayer.frame[3+lengthAfterStuffing]  = BCC2;
	linkLayer.frame[4+lengthAfterStuffing] = FLAG;
	
	return (int) write(linkLayer.fd, linkLayer.frame, lengthAfterStuffing+6); // "+6" pois somamos 2FLAG,2BCC,1A,1C
	printf("[sendinf] END\n");
}

int receiveframe(char *data, int * length) {
	printf("[receiveframe] START\n");
	char* charread = malloc(1); //char in serial port
	
	int state = 0; //state machine state
	char Aread, Cread; //adress and command
	int stop = FALSE; //control flag
	int Rreturn; //for return
	int Type; //type of frame received
	int num_chars_read = 0; //number of chars read


	//State machine
	while(stop == FALSE) { 
		Rreturn = read(linkLayer.fd, charread, 1); //read 1 char
		
		if (Rreturn < 0) return -1; //nothing
		
		switch(state) {
			case 0:{ //State Start
				printf("[receiveframe] START\n");
				if(*charread == FLAG) state = 1;
				break;
			}
			
			case 1:{ //Flag -> expect address
				if(*charread == A) {
					printf("[receiveframe] ADRESS\n");
					state = 2;
					Aread = *charread;
				} else if(*charread == FLAG) state = 1; //another flag
				break;
			}
			
			case 2:{ //Address -> Command (many commands possible)
				printf("[receiveframe] COMMAND\n");
				Cread = *charread;
				if(*charread == SET)  {
						Type = SET_RECEIVED;
						state = 3;
					}
				
				else if(*charread == UA) {
					printf("[receiveframe] UA\n");
					Type = UA_RECEIVED;
					state = 3;
					}
				
				else if(*charread == DISC) {
					printf("[receiveframe] DISC\n");
					Type = DISC_RECEIVED;
					state = 3;
				}
				
				else if(*charread == (RR | (linkLayer.sequenceNumber << 7))) {
					printf("[receiveframe] RR\n");
					Type = RR_RECEIVED;
					state = 3;
				}
				
				else if(*charread == (REJ | (linkLayer.sequenceNumber << 7))) {
					printf("[receiveframe] REJ\n");
					Type = REJ_RECEIVED;
					state = 3;
				}
				
				else if(*charread == (linkLayer.sequenceNumber << 6)) {
					printf("[receiveframe] DATA\n");
					Type = DATA_RECEIVED;
					state = 3;
				}
				
				else if(*charread == FLAG) state = 1;
				else state = 0;

			}
				
			case 3:{ //command -> BBC1
				if(*charread == FLAG) state = 1;
				
				else if(*charread != (Aread ^ Cread)) state = 0; //bcc 
				
				else 
				{
					if(Type == DATA_RECEIVED) state = 4;
					else state = 6;						
				}
			}
				
			case 4:{ //Data expected
					
				if (*charread == FLAG)
					{
						char BCC2exp = data[0];
						int j;
						for(j = 1;j < num_chars_read - 1; j++) BCC2exp = (BCC2exp ^ data[j]);
						if(data[num_chars_read -1] != BCC2exp) {
							sendSupervisionFrame(linkLayer.fd,REJ);
							return -1;
						}
						else
						{
							*length = num_chars_read - 1;
							sendSupervisionFrame(linkLayer.fd, RR);
							linkLayer.sequenceNumber = linkLayer.sequenceNumber ^ 1;
						}
						
						stop = TRUE;
						state = 0;
					}
					
					else if(*charread == ESC) state = 5; //deshuffel o proximo
					
					else 
					{
						data[num_chars_read] = *charread;
						num_chars_read++;
						state = 4;
					}
					
					break;
				}
			
			case 5:{ //Destuffing
				data[num_chars_read] = *charread ^ STFF;
				num_chars_read++;
				state = 4;
				break;
			}
			
			case 6:{ //Flag
				if(*charread == FLAG)
				{ //flag
					stop = TRUE;
					state = 0;
				}
				else state = 0;
				break;
			}
			printf(",%d]", state);
		}
	}
	printf(" Done\n");
	free(charread);
	return Type;
					
}


int llopen(int fd, int txrx) {
	printf("[LLOPEN] START\n");
	
	linkLayer.sequenceNumber = 0;
	
	if(txrx == TRANSMITTER) {
		
		(void) signal(SIGALRM, atende);
		linkLayer.numTransmissions = 0;
		linkLayer.timeout = MAXT;
		
		while(linkLayer.numTransmissions < MAXT ){	
		
			if(alarm_flag){
				
				sendSupervisionFrame(fd, (unsigned char) SET); 
				alarm(linkLayer.timeout);                 				// activa alarme de 3s
				alarm_flag=0;
			}
	
			if( receiveframe(NULL,NULL) != UA_RECEIVED ) return -1;
			else if(receiveframe(NULL,NULL) == UA_RECEIVED) return fd;
			
		}
		return -1;	
		
	}
	
	else if (txrx == RECEIVER) {
		
		printf("Waiting for SET\n");
		if(receiveframe(NULL,NULL) == SET_RECEIVED) printf("SET_RECEIVED");
	
		sendSupervisionFrame(fd,UA);
		printf("UA sent\n");
		
		return fd;
	}
	
	return 0;
}

int llread(int fd, char* buffer){
	printf("[LLREAD] START\n");
	int num_chars_read = 0;

	int Type = receiveframe(buffer, &num_chars_read);


	if(Type == DISC_RECEIVED){
		sendSupervisionFrame(fd, DISC);
		
		while(receiveframe(NULL,NULL) != UA_RECEIVED);
		
		//llclose(linkLayer.fd);
			
		return 0;
		
	}
	else if(Type == DATA_RECEIVED)	{
		return num_chars_read;
	}

	return -1;
}

int llwrite(int fd, unsigned char* buffer, int length){
    printf("[LLWRITE] START\n");
    
	linkLayer.timeout = 0;
	int CompleteFrames =  length / MAX_SIZE;
	int remainingBytes =  length % MAX_SIZE;
	int flag = 1;
	
	(void) signal(SIGALRM, atende);
	linkLayer.numTransmissions = 0;
	linkLayer.timeout = MAXT;
	
	int i;
	for(i = 0; i < CompleteFrames; i++){
	
		flag = 1;
		while(linkLayer.numTransmissions < MAXT && flag) {	
		
			if(alarm_flag){
				
				sendInformationFrame(buffer + (i * MAX_SIZE), MAX_SIZE);
				alarm(linkLayer.timeout);                 				// activa alarme de 3s
				alarm_flag=0;
			}
	
			if(receiveframe(NULL,NULL) != RR_RECEIVED) {
				 if(receiveframe(NULL,NULL) == REJ_RECEIVED) linkLayer.numTransmissions = 0;
				 else return -1;
			}
			else if(receiveframe(NULL,NULL) == RR_RECEIVED) flag =0;
			
		}
		
		
	}
	
	if(remainingBytes > 0){
		printf("Wait, theres one more\n");
		flag = 1;
		while(linkLayer.numTransmissions < MAXT && flag) {	
		
			if(alarm_flag){
				
				sendInformationFrame(buffer + (i * MAX_SIZE), remainingBytes);
				alarm(linkLayer.timeout);                 				// activa alarme de 3s
				alarm_flag=0;
			}
	
			if(receiveframe(NULL,NULL) != RR_RECEIVED) {
				 if(receiveframe(NULL,NULL) == REJ_RECEIVED) linkLayer.numTransmissions = 0;
				 else return -1;
			}
			else if(receiveframe(NULL,NULL) == RR_RECEIVED) flag =0;
			
		}
		
	}
	printf("[LLWRITE] END\n");
	return 0;

}

int llclose(int fd, int txrx){
	
	return 1;
}

int main (int argc, char** argv) {

	int c, res;
	struct termios oldtio,newtio;
	char buf[255];
	int i, rs, sum = 0, speed = 0;
	

	
	if ( (argc < 2) || 
	 	((strcmp("/dev/ttyS0", argv[1])!=0) && 
	 	(strcmp("/dev/ttyS4", argv[1])!=0) )) {
	 	printf("Usage:\tnserial SerialPort\n\tex: nserial /dev/ttyS1\n");
	 	exit(1);
	 }
	

	/*
	Open serial port device for reading and writing and not as controlling tty
	because we don't want to get killed if linenoise sends CTRL-C.
	*/


	fd = open(argv[1], O_RDWR | O_NOCTTY );
	if (fd <0) {perror(argv[1]); exit(-1); }

	if ( tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
		perror("tcgetattr");
		exit(-1);
	}

	bzero(&newtio, sizeof(newtio));
	newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;

	// set input mode (non-canonical, no echo,...) 
	newtio.c_lflag = 0;

	newtio.c_cc[VTIME]    = 1;   /* inter-character timer unused */
	newtio.c_cc[VMIN]     = 0;   /* blocking read until 5 chars received*/



	  
	   // VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a 
	    //leitura do(s) próximo(s) caracter(es)
	  



	tcflush(fd, TCIOFLUSH);

	if ( tcsetattr(fd,TCSANOW,&newtio) == -1) {
		perror("tcsetattr");
		exit(-1);
	}
	

	printf("Reciver - 0\nTransmitter -1\n");
	scanf("%d", &txrx);
	
	printf("opening llopen with %d\n", txrx);
	rs = llopen(fd, txrx);
	linkLayer.fd = rs;
	printf("result : %d\n", rs);
	
	if(txrx == TRANSMITTER)
	{
		unsigned char buffer[3] = "ola";
		llwrite(rs, buffer, 3);
	}
	else
	{
		char buffer[3];
		llread(fd,buffer);
	}
	return 1;
}
	
