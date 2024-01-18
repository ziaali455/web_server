/*
 * tcp-recver.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
static void die(const char *s) { perror(s); exit(1); }

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s <server_port> <web_root> <mdb-lookup-host> <mdb-lookup-port>\n", argv[0]);
        exit(1);
    }

    unsigned short port = atoi(argv[1]);

    // Create a listening socket (also called server socket) 

    int servsock;
    if ((servsock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        die("socket failed");

    // Construct local address structure

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // any network interface
    servaddr.sin_port = htons(port);

    // Bind to the local address

    if (bind(servsock, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
        die("bind failed");

    // Start listening for incoming connections

    if (listen(servsock, 5 /* queue size for connection requests */ ) < 0)
        die("listen failed");

    int clntsock;
    socklen_t clntlen;
    struct sockaddr_in clntaddr;
    
    //part 2b: server acts as client
    unsigned short mdbPort = atoi(argv[4]);
    
    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        die("socket failed");

    struct hostent *he;
    char *serverName = argv[3];
    // get server ip from server name
    if ((he = gethostbyname(serverName)) == NULL) {
        die("gethostbyname failed");
    }
    char *serverIP = inet_ntoa(*(struct in_addr *)he->h_addr);

    struct sockaddr_in mdbaddr;
    memset(&mdbaddr, 0, sizeof(mdbaddr)); // must zero out the structure
    mdbaddr.sin_family      = AF_INET;
    mdbaddr.sin_addr.s_addr = inet_addr(serverIP);
    mdbaddr.sin_port        = htons(mdbPort); // must be in network byte order
    
    if (connect(sock, (struct sockaddr *) &mdbaddr, sizeof(mdbaddr)) < 0)
        die("mdb-lookup-server connection failed");

    char buf[4096];
    FILE* resultsFile = fdopen(sock, "r");
    while (1) {

	// Part 1a
        // Accept an incoming connection

        clntlen = sizeof(clntaddr); // initialize the in-out parameter

        if ((clntsock = accept(servsock,
                        (struct sockaddr *) &clntaddr, &clntlen)) < 0){
            die("accept failed");
	}
	
	//Get the first line which is in the form: GET /path/file.html HTTP/1.0
	FILE *httpClientInput = fdopen(clntsock, "r");

/*	if(fgets(buf, sizeof(buf),httpClientInput)==NULL){
	       if(ferror(httpClientInput)){
	       		fprintf(stderr, "Bad file socket");
			fclose(httpClientInput);
			close(clntsock);
			continue; 
	       }else{
		     fclose(httpClientInput);
	       }
	}*/
	if (fgets(buf, sizeof(buf), httpClientInput) == NULL) {
    		if (feof(httpClientInput)) {
        	// Handle end of file (browser connection closed)
        	fclose(httpClientInput);
        	close(clntsock);
        	continue;
    		}else if (ferror(httpClientInput)) {
		// Handle file error
        	fclose(httpClientInput);
       		close(clntsock);
        	continue;
    		}	
	}
	char temp[4096];
	while(fread(temp, 2, 1, httpClientInput)>1){
		if(strncmp("\r\n", temp, strlen("\r\n"))){
			break;
		}
	}

	//SOME CODE OMITTED FOR ACADEMIC INTEGRITY REASONS	



	//prepend webroot to uri request
	char fullPath[4096];
	snprintf(fullPath, sizeof(fullPath), "%s%s", argv[2], requestURI);
	
	
	//check if it is a directory and if so and there's a /, tack on the index.html
	//if there's no /, there's an issue and we throw an error
	struct stat fileStat;
	//fprintf(stderr, "your full path is: %s\n", fullPath); 
	if(stat(fullPath, &fileStat)==0){
		if (S_ISDIR(fileStat.st_mode) && fullPath[strlen(fullPath) - 1]=='/') {
			strcat(fullPath, "index.html");

        	}else if(S_ISDIR(fileStat.st_mode) && fullPath[strlen(fullPath) - 1]!='/'){
			int res = snprintf(errResponse, sizeof(errResponse), "HTTP/1.0 501 Not Implemented\r\n\r\n<html><body>\r\n<h1>501 Not Implemented</h1>\r\n</body></html>\r\n");
			if (res < 1){
                        	die("Error formatting 501 response");
                	}
                	//fwrite(errResponse, 1, res, input);
                	send(clntsock, errResponse, strlen(errResponse), 0);
			fclose(httpClientInput);
			close(clntsock);
                	continue;
		}

	}
	


	//send the HTTP header required for any message
	char* okHeaderMessage = "HTTP/1.0 200 OK\r\n\r\n";
	//fprintf(stdout, "%s \"%s %s %s\" 200 OK\n", inet_ntoa(clntaddr.sin_addr), method, requestURI, httpVersion);

	//when the request is simply mdb lookup, send the submit form
	if(strcmp(requestURI, "/mdb-lookup")==0){
		const char *form =
	    "<html><body>\n"
            "<h1>mdb-lookup</h1>\n"
            "<p>\n"
            "<form method=GET action=/mdb-lookup>\n"
            "lookup: <input type=text name=key>\n"
            "<input type=submit>\n"
            "</form>\n"
            "<p>\n";
		send(clntsock, okHeaderMessage, strlen(okHeaderMessage),0);
		fprintf(stdout, "%s \"%s %s %s\" 200 OK\n", inet_ntoa(clntaddr.sin_addr), method, requestURI, httpVersion);
		send(clntsock, form, strlen(form),0); 
	}else if(strstr(requestURI, "/mdb-lookup?key=")!=NULL){
		//send query to mdb lookup server along with a \n
		char* query = strstr(requestURI, "=") + 1;
		char cleanedQuery[4096];
		snprintf(cleanedQuery, sizeof(cleanedQuery), "%s\n", query);
		int mdbSendRes = 0;
		signal(SIGPIPE, SIG_IGN);
		fprintf(stderr, "looking up [%s]: ", query);
		if((mdbSendRes = send(sock, cleanedQuery, strlen(cleanedQuery),0)) != strlen(cleanedQuery)){
			
			perror("send() failed");
			//fprintf(stdout, "%s \"%s %s %s\" 500 Internal Server Error\n", inet_ntoa(clntaddr.sin_addr), method, requestURI, httpVersion);
		 	int res = snprintf(errResponse, sizeof(errResponse), "HTTP/1.0 500 Internal Server Error\r\n\r\n<html><body>\r\n<h1>500 Internal Server Error</h1>\r\n</body></html>\r\n");
			if (res < 1){
                        	die("Error formatting 501 response");
                	}
                	send(clntsock, errResponse, strlen(errResponse), 0);
			fclose(httpClientInput);
			close(clntsock);
                	continue;
		}else{
			fprintf(stdout, "%s \"%s %s %s\" 200 OK\n", inet_ntoa(clntaddr.sin_addr), method, requestURI, httpVersion);

		}
		
		//receive the results from the query back
//		FILE* resultsFile = fdopen(sock, "r");

		if(resultsFile==NULL){
			//fprintf(stdout,"mdb-lookup-server connection terminated");
			fprintf(stdout, "%s \"%s %s %s\" 500 Internal Server Error\n", inet_ntoa(clntaddr.sin_addr), method, requestURI, httpVersion);
		 	int res = snprintf(errResponse, sizeof(errResponse), "HTTP/1.0 500 Internal Server Error\r\n\r\n<html><body>\r\n<h1>500 Internal Server Error</h1>\r\n</body></html>\r\n");
			if (res < 1){
                        	die("Error formatting 501 response");
                	}
                	//fwrite(errResponse, 1, res, input);
                	send(clntsock, errResponse, strlen(errResponse), 0);
			fclose(httpClientInput);
			close(clntsock);
                	continue;

		} /*else{
			fprintf(stdout, "%s \"%s %s %s\" 200 OK\n", inet_ntoa(clntaddr.sin_addr), method, requestURI, httpVersion);
		} */
	 	
		const char *form =
		"<html><body>\n"
            	"<h1>mdb-lookup</h1>\n"
            	"<p>\n"
            	"<form method=GET action=/mdb-lookup>\n"
            	"lookup: <input type=text name=key>\n"
            	"<input type=submit>\n"
            	"</form>\n"
            	"<p>\n"
		"<p><table border>\n";
	 	send(clntsock, okHeaderMessage, strlen(okHeaderMessage),0);
                send(clntsock, form, strlen(form),0); 	
		char line[4096];
		int count = 1;
                while (fgets(line, sizeof(line), resultsFile) != NULL && strcmp(line, "\n")!=0 && strlen(line)!=1) {
       			/*if(strcmp(line, "\n")!=0){
				break;
			} */

       			//printf("%s", line);
			char htmlLine[4120];
			if(count%2==0){
				snprintf(htmlLine, sizeof(htmlLine), "<tr><td bgcolor=yellow>%s\n",line);
			}else{
				snprintf(htmlLine, sizeof(htmlLine), "<tr><td>%s\n",line);
			}
			send(clntsock, htmlLine, strlen(htmlLine) + 1,0);
			count++;
/*			if(strcmp(line, "\n")==0){
				break;
			} */

                }

		const char *endForm = 
		"</table>\n"
		"</body></html>\n";
		send(clntsock, endForm, strlen(endForm),0);
		
		close(clntsock);
		//fclose(resultsFile);			
		//open socket as a file with fread ad fdopen.
	}else{
		//now we can open the file corresponding to this request, read it in, and send it to the client
        	FILE *requestedFile = fopen(fullPath, "rb");
        	if(requestedFile==NULL){
                	//fprintf(stderr, "requested file not found\n");
                	fprintf(stdout, "%s \"%s %s %s\" 404 Not Found\n", inet_ntoa(clntaddr.sin_addr), method, requestURI, httpVersion);
                	int res = snprintf(errResponse, sizeof(errResponse), "HTTP/1.0 404 Not Found\r\n\r\n<html><body>\r\n<h1>404 Not Found</h1>\r\n</body></html>\r\n");
                	if (res < 1){ 
                	        die("Error formatting 404 response");
                	}           
                	//fwrite(errResponse, 1, res, input);
                	send(clntsock, errResponse, strlen(errResponse), 0);
                	fclose(httpClientInput);
                	close(clntsock);
                	continue;
        	}else{      
                	fprintf(stdout, "%s \"%s %s %s\" 200 OK\n", inet_ntoa(clntaddr.sin_addr), method, requestURI, httpVersion);

        	}	

		send(clntsock, okHeaderMessage, strlen(okHeaderMessage),0);
		//while loop advances through the header section of html file
		/*char header[4096];
		while(1){
                	if(fgets(header, sizeof(header) , httpClientInput)==NULL){
                        	if(!ferror(httpClientInput)){
                        //        	fclose(httpClientInput);
                        //        	close(clntsock);
					break;
                        	}	
               			die("file error");         
                	}
                	if(strcmp(header, "\r\n") ==0){
                        	break;
                	}
        	}*/ /*
		while (fgets(header, sizeof(header), httpClientInput) != NULL) {
    			if (strcmp(header, "\r\n") == 0) {
       				break;  // End of headers
    			}
		}

		if (ferror(httpClientInput)) {
    			perror("fgets error");
    			fclose(httpClientInput);
   			close(clntsock);
			continue;
		}*/
		//read and write from requested html file to the browser
		char outputBuf[4096];
		size_t n;
		//fprintf(stderr, "we got to the point where we send stuff back to client");
		while ((n = fread(outputBuf, 1, sizeof(outputBuf), requestedFile)) > 0) {
	//		if(outputBuf[0] != '\n'){
			//fprintf(stderr,"%s", outputBuf);
			if(send(clntsock, outputBuf, n, 0) != n){
				perror("send failure");
				break;
			}
	//		}
		}
		fclose(requestedFile);	
	
	
	}

        fclose(httpClientInput);
    	close(clntsock);
    }
   close(servsock);
   close(sock); 
}
