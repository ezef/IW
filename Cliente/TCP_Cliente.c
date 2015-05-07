#include <stdio.h>
#include <stdlib.h>
#include <string.h>  
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>  
#include <pthread.h>

#define BUFFER_TAMANIO 1406
#define DATOS 1300
#define PUERTO 2000

/*FECHA 060215
opciones del protocolo:
* 2 : del menu
* 5 : enviar_nombres
* 16: solicitud de archivo al cliente propietario
* 17: devolucion del archivo al cliente propietario
*/

struct mensaje{
	
	uint16_t opcion;	
	uint16_t leidos; 
	char nombreArchivo[100];	
	char buff[DATOS];
};


int leer_mensaje ( int sd, char *buffer, int total ) {
    
    int bytes;
    
    int leido;
    leido = 0;
    bytes = 1;
    while ( (leido < total) && (bytes > 0) ) {
        bytes = recv ( sd , buffer + leido , total - leido , 0);
        leido = leido + bytes;
    }
    return ( leido );
}

//busca el archivo solicitado en la carpeta solicitada y lo envia al sdc
void enviarArchivo(int sdc,char *nombre, char *carpeta){ 
		FILE *f1;		
		int leidos = 0;
		char buffer[BUFFER_TAMANIO];
		struct mensaje *str;
		str = (struct mensaje *) buffer;
		char c[100];
		strcpy(c,carpeta);		
		
		f1 = fopen(strcat(c,nombre), "r");		
		
		if ( !ferror(f1)){
			strcpy(str->nombreArchivo, nombre);				
			leidos = fread(str->buff, 1,DATOS,f1);
			while(leidos != 0){			
				str->leidos = htons(leidos);			
				send ( sdc , buffer, BUFFER_TAMANIO, 0 );
				leidos = fread(str->buff, 1,DATOS,f1);	
			}			
		}
		else{perror("error durante la lectura");}		
		fclose(f1);
		

}

//recibe el archivo enviado de sd y lo guarda en carpeta. devuelve el nombredel archivo mediante el parametro nombre.
void recibirArchivo(int sd, char *nombre, char *carpeta){  
		FILE *f1;				
		int leidos = 0;
		char buffer[BUFFER_TAMANIO];
		struct mensaje *str;
		char c[100];
		
		strcpy(c,carpeta);		
		str = (struct mensaje *) buffer;		
		leidos = leer_mensaje(sd,buffer,BUFFER_TAMANIO);			
		strcpy(nombre,str->nombreArchivo);				
		f1 = fopen(strcat(c,str->nombreArchivo), "w+");	
				
		while(ntohs(str->leidos) == DATOS){			
			fwrite(str->buff,1,ntohs(str->leidos),f1);
			leidos = leer_mensaje(sd,buffer,BUFFER_TAMANIO);			
		}		
		fwrite(str->buff,1,ntohs(str->leidos),f1);		
		fclose(f1);
}
// lee los nombres de los archivos en un directorio y los envia al sd
void enviar_nombres(int sd){ 
	DIR *dir;
	struct dirent *ent;
	char buffer[DATOS]="";
	char buff[BUFFER_TAMANIO];
	int i;
	
		if ((dir = opendir ("archivos")) != NULL) {						
			while ((ent = readdir(dir)) != NULL) {														
				if (ent->d_type == DT_REG){					
					strcat(buffer,ent->d_name);
					strcat(buffer,",");
					i++;
				}								
			}						
		closedir (dir);		  
		} 
		else {perror ("error abriendo directorio");}		
		
		struct mensaje *str = (struct mensaje *) buff;
		strcpy(str->buff,buffer);
		str->opcion = htons(5);
		send ( sd, buff, BUFFER_TAMANIO, 0 );
}
//servidor de escucha en el cliente para recibir solicitudes y devoluciones de archivos propios
void *tmain(void *arg){
	int n;
	int sd;
	int sdc;
	int lon;
	char buffer[BUFFER_TAMANIO];
	char nom[100];
	
	struct sockaddr_in servidor;
	struct sockaddr_in cliente;
	struct mensaje *string=(struct mensaje *) buffer;

	servidor.sin_family = AF_INET;			
	servidor.sin_port = htons(2001);	
	servidor.sin_addr.s_addr = INADDR_ANY;
	
	sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	
	int so_reuseaddr = 1; 
	int z = setsockopt(sd,SOL_SOCKET,SO_REUSEADDR,&so_reuseaddr,sizeof(so_reuseaddr));
	
	if (bind(sd, (struct sockaddr *) &servidor, sizeof(servidor)) < 0) {
		perror("Error en bind");
		exit(-1);
	}
	
	listen ( sd, 1 );
	
		while (1){
			lon = sizeof(cliente);		
			sdc = accept( sd, (struct sockaddr *) &cliente, &lon );
			
			while (leer_mensaje(sdc,buffer,BUFFER_TAMANIO) > 0) {				
				
				if(ntohs(string->opcion) == 16){// el S solicita archivo, se lo envia.					
					enviarArchivo(sdc,string->nombreArchivo,"archivos/");										
				}
				
				if(ntohs(string->opcion) == 17){// el S devuelve un archivo perteneciente al cli.					
					recibirArchivo(sdc, &nom,"archivos/");								
				}			
			}
			
		}
	close(sdc);	
	
	
	close(sd);
	
}

//codigo principal del cliente 
int main(int argc, char *argv[]){
	int err = mkdir("archivos",0777); 
	mkdir(".temp",0777);
	int sd;
	int lon;
	int n;
	
	struct mensaje *string;
	char buffer[BUFFER_TAMANIO];
	struct sockaddr_in servidor;
	struct sockaddr_in cliente;
	struct hostent *nodoRemoto;

	sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);	
	servidor.sin_family = AF_INET;
	servidor.sin_port = htons(PUERTO);

	if ( nodoRemoto = gethostbyname ( argv [1] ) ) {
		memcpy ( &servidor.sin_addr , nodoRemoto->h_addr , nodoRemoto->h_length );
	}
	
	lon = sizeof(servidor);	
	
	if (connect ( sd , (struct sockaddr *) &servidor, lon ) < 0 ) {
		perror ("Error en connect");
		exit(-1);
	}
	
	
	//thread para atender solicitudes y devoluciones de archivos por el puerto 2001
	pthread_t tid;
	pthread_create(&tid, NULL, tmain, NULL);
	
	enviar_nombres(sd); // envia nombres de archivos propios al servidor	
	
	int opcion=0;
	string=(struct mensaje *) buffer;
	while (opcion != 1) {
		system("clear");
		printf("INGRESE OPCION DESEADA \n1:salir\n2:editar archivo\n");
		scanf("%d", &opcion);
		string->opcion = htons(opcion);		
		
		switch (opcion){
		case 1:
			printf("Tchüss\n");
			close(sd);
			break;
		case 2:
			// codigo para la edicion de un archivo perteneciente al sistema distribuido
			
			send ( sd, buffer, BUFFER_TAMANIO, 0 ); //envia la opcion 2 al S
			leer_mensaje( sd , buffer , BUFFER_TAMANIO); // recibe la lista de archivos
			
			//parsea la lista de archivos separada por comas e imprime cada uno de los nombres
			char dat[DATOS];
			strcpy(dat,string->buff);
			int i = 1;
			char *ele = strtok(dat,",");
				while (ele !=NULL){
					printf("%i.%s \n",i,ele);								
					ele = strtok(NULL,",");
					i++;						
				}
				
			int o;			
			printf("seleccione un numero de archivo a editar\n");
			scanf("%i", &o);			
			
			//busca que nombre de archivo pertenece al numero elegido
			ele = strtok(string->buff,",");			
			for(i=1;i < o;i++){							
				ele = strtok(NULL,",");								
			}		
			
			//envia el nombre de archivo solicitado
			strcpy(string->nombreArchivo, ele);			
			send ( sd, buffer, BUFFER_TAMANIO, 0 );
			
			//recibe el archivo de parte del S
			char n[100];
			recibirArchivo(sd,&n,".temp/");
			
			//ejecuta el editor nano para editarlo
			char command[105] = "nano ";
			strcat(command,".temp/");			
			system(strcat(command,n));			
			
			//envia el archivo nuevamente al S
			enviarArchivo(sd,n,".temp/");
			char car[100]=".temp/";
			remove(strcat(car,n));
			
			
			
			break;		
			
		default: 
			printf("opcion incorrecta\n");
			break;
		
				
					
		}
	
	}	
}
