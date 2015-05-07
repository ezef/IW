#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>  
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <netdb.h>

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


// estructura dinamica que manejara el servidor 
// para almacenar todos los nombres de archivos de los clientes, sus sd
// y si el archivo esta en uso o no.
struct larchivos{     
	char nombre[100]; 
	int sd;
	int uso;
	struct larchivos *sig;
};

// Función que se encarga de leer un mensaje de aplicacion completo 
// lee exactamente la cantidad de bytes que se pasan en el argumento total:
int leer_mensaje ( int sd, char * buffer, int total ) {
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

//busca el archivo solicitado y lo envia al sdc
void enviarArchivo(int sdc, char *nombre){ 
		FILE *f1;		
		int leidos = 0;
		char buffer[BUFFER_TAMANIO];
		struct mensaje *str;
		str = (struct mensaje *) buffer;
		char c[100] =".temp/";
		f1 = fopen(strcat(c,nombre), "r");		
				
		strcpy(str->nombreArchivo, nombre);			
		leidos = fread(str->buff, 1,DATOS,f1);
		
		while(leidos != 0){			
			str->leidos = htons(leidos);		
			send( sdc , buffer, BUFFER_TAMANIO, 0 );			
			leidos = fread(str->buff, 1,DATOS,f1);	
		}
		
		
		if ( ferror(f1)){
			perror("error durante la lectura");
		}		
		fclose(f1);		

}

//recibe el archivo enviado de sd y lo guarda en carpeta. devuelve el nombre del archivo mediante el parametro nombre.
void recibirArchivo(int sd, char *nombre, char * carpeta){ 
		FILE *f1;				
		int leidos = 0;
		char buffer[BUFFER_TAMANIO];
		struct mensaje *str;
		str = (struct mensaje *) buffer;	
		char c[100];
		
		strcpy(c,carpeta);		
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

//metodo para insertar elemento en la lista. inserta en la cabeza
struct larchivos * insele(struct larchivos *p,char * nom,int sd){
	struct larchivos *l = (struct larchivos *) malloc( sizeof(struct larchivos) );
	strcpy(l->nombre,nom);
	l->sd     = sd;
	l->uso	  =0;
    l->sig    = p;
	return l;
}

//metodo para imprimir la lista de archivos
void printl(struct larchivos *p){
	if(p==NULL){ printf("LISTA: vacia\n");}else{printf("LISTA: \n");}
	while (p!=NULL){
		printf("nom:%s \t\t sd:%i\n",p->nombre, p->sd);
		p=p->sig;
	}
	
	
}

//metodo para buscar que sd tiene el archivo solicitado
int buscarSD(struct larchivos *p, char * nombre){
	
	while (strcmp(nombre,p->nombre) !=0 ){
		p=p->sig;
	}
	return p->sd;
	
}

//metodo para marcar si el archivo esta en uso o no.0 si no esta en uso
// 1 si lo esta.
void marcarUso(struct larchivos *p, int uso, char * nombre){
	while (strcmp(nombre,p->nombre) != 0){
		p=p->sig;		
	}
	p->uso=uso;	
	
}

//metodo para eliminar los nodos de la lista cuando un cliente se desconecta
struct larchivos * eliminar(struct larchivos *p,int sd){
	struct larchivos *aux=NULL;
	struct larchivos *head=p;
		
	while (p != NULL){		
		if (p->sd == sd){
			if(aux==NULL){
				head  = p->sig;
				free(p);
				p=head;												
			}
			else{
				aux->sig = p->sig;
				free(p);
			    p=aux->sig;								
			}
		}
		else {
			aux = p;
		    p   = p->sig;
		}
	}
	return head;
}

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
struct larchivos *l = NULL;

//metodo principal por cada thread que se cree.
void *tmain(void *arg){      
	char buffer[BUFFER_TAMANIO];
	int n;
	int sdc = *(int *) arg;		
	
	struct mensaje *str=(struct mensaje *) buffer;
	n=leer_mensaje (sdc, buffer, BUFFER_TAMANIO );
	
	while (ntohs(str->opcion) != 1 && n>0) {
		
		switch (ntohs(str->opcion)){
		case 1:
			//opcion de salida, no entra al while				
			break;
		case 2:{
			//enviar lista de archivos separado con coma
			char b[DATOS]="";
			struct larchivos *a=l;  			
			while (a != NULL){
				if(a->uso == 0){ //si esta siendo editado no lo muestra
					strcat(b,a->nombre);
					strcat(b,",");
				}
				a=a->sig;										
			}				
			strcpy(str->buff,b);				
			send ( sdc , buffer, BUFFER_TAMANIO, 0 );  //envia el listado de archivos disponibles
			
			//lee el archivo solicitado por el cliente
			leer_mensaje (sdc, buffer, BUFFER_TAMANIO );			
			
			marcarUso(l,1,str->nombreArchivo);//se marca como archivo en uso				
			
			//se crea una conexion hacia el cliente que posee el archivo
			int sdsc;
			int lon;				
			struct sockaddr_in ser;
			struct sockaddr_in cli;			
			
			sdsc = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);			
			
			lon = sizeof(ser);
							
			getpeername(buscarSD(l,str->nombreArchivo),&ser, &lon);
			ser.sin_family = AF_INET;
			ser.sin_port = htons(2001);
						
			lon = sizeof(ser);
			if (connect ( sdsc , (struct sockaddr *) &ser, lon ) < 0 ) {
				perror ("Error en connect");				
				exit(-1);
			}
			
			//se le envia la opcion de solicitud de archivo
			str->opcion=htons(16);
			send( sdsc , buffer, BUFFER_TAMANIO, 0 );
			
			//recibe el archivo
			char n[100];
			recibirArchivo(sdsc,&n,".temp/");
							
			//envia el archivo al cliente solicitante
			enviarArchivo(sdc,n);
			char nom[100] = ".temp/";
			remove(strcat(nom,n));
			
			//recibe el archivo editado
			recibirArchivo(sdc,&n,".temp/");
			
			//envia el archivo editado al propietario y desmarca su uso
			str->opcion=htons(17);
			send(sdsc, buffer, BUFFER_TAMANIO, 0 );
			enviarArchivo(sdsc,n);
			marcarUso(l,0,str->nombreArchivo);			
			remove(nom);
			
							
			close(sdsc);
			}
			break;	
		
		case 5:
		{	//recibe el listado de archivos de un cliente que se acaba de conectar
			pthread_mutex_lock(&mutex);
			char *ele = strtok(str->buff,",");
			while (ele !=NULL){
				l=insele(l,ele,sdc);								
				ele = strtok(NULL,",");						
			}
			printl(l);	
			pthread_mutex_unlock(&mutex);
			}					
			break;
		}
		
	n=leer_mensaje (sdc, buffer, BUFFER_TAMANIO );
	
	}
	//se elimina de la lista los archivos del cliente desconectado
	pthread_mutex_lock(&mutex);	
	l=eliminar(l,sdc);
	printl(l);	
	pthread_mutex_unlock(&mutex);
	close (sdc);		
}

//codigo del hilo principal
int main()
{
	printf("SERVIDOR \n");
	mkdir(".temp",0777);
	int n;
	int sd;
	int sdc;
	int lon;
	
	struct sockaddr_in servidor;
	struct sockaddr_in cliente;
	struct mensaje *string;

	servidor.sin_family = AF_INET;			
	servidor.sin_port = htons(PUERTO);		
	servidor.sin_addr.s_addr = INADDR_ANY;	
	
	pthread_t tid;
	pthread_t tid_ver;	

	sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	
	//-- fuerza la reutilizacion de la direccion para q no cuelge el bind si crashea.
	int so_reuseaddr = 1; 
	int z = setsockopt(sd,SOL_SOCKET,SO_REUSEADDR,&so_reuseaddr,sizeof(so_reuseaddr)); 
	//--
	
	if (bind(sd, (struct sockaddr *) &servidor, sizeof(servidor)) < 0) {
		perror("Error en bind");
		exit(-1);
	}
	
	listen ( sd, 1 );

	while (1) {

		lon = sizeof(cliente);		
		sdc = accept( sd, (struct sockaddr *) &cliente, &lon );			
		pthread_create(&tid, NULL, tmain, &sdc);	
		
	}
	
	close(sd);

}

























    
