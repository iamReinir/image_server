#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <microhttpd.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <cjson/cJSON.h>

#define HOSTNAME "http://localhost:8888"
#define PORT 8888
#define MAX_FILE_SIZE 209715200  // 200 MB
#define UPLOAD_DIR "./uploads"  // Directory to store uploaded files
#define THREAD_POOL_SIZE 4
#define AUTO_GENERATE_FILENAME true

typedef struct {
    char hostname[256];
    int port;
    size_t max_file_size;
    char upload_dir[256];
    int thread_pool_size;
    bool auto_generate_filename;
} Config;

Config server_config = {HOSTNAME, PORT, MAX_FILE_SIZE, UPLOAD_DIR,THREAD_POOL_SIZE,AUTO_GENERATE_FILENAME};

struct connection_info_struct {
    FILE *fp;
    char *filename;
	struct MHD_PostProcessor *post_processor;
};



static enum MHD_Result handle_file_upload(void *cls, 
                                           enum MHD_ValueKind kind, 
                                           const char *key,
                                           const char *filename,
                                           const char *content_type, 
                                           const char *transfer_encoding,
                                           const char *upload_data, 
                                           uint64_t off,
										   size_t size) {
    if (!filename || strlen(filename) == 0) {
        return MHD_NO;
    }                                   
    struct connection_info_struct *con_info = cls;    
	if(filename)
	{			
		// Open the file if it hasn't been opened yet
		if (con_info->fp == NULL) {
            if(!server_config.auto_generate_filename)
                snprintf(con_info->filename, 256, "%s/%s", server_config.upload_dir, filename);
            else
                snprintf(con_info->filename, 256, "%s/uploaded_%ld",server_config.upload_dir, (long)time(NULL));
            printf("filename:%s\n",con_info->filename);
			con_info->fp = fopen(con_info->filename, "wb");
			if (con_info->fp == NULL) {
				return MHD_NO;  // Failed to open file
			}
		}

		// Write the uploaded data to the file
		fwrite(upload_data, 1, size, con_info->fp);
	}

    return MHD_YES;
}
/*

static int get_filename(void *cls, enum MHD_ValueKind kind, const char *key, const char *value) {
    printf("Header Received: %s = %s\n", key, value ? value : "(null)");  
    if (strcmp(key, "Content-Disposition") == 0) {
        const char *filename_pos = strstr(value, "filename=");
        if (filename_pos) {
            filename_pos += 9;  // Move past 'filename='
            if (*filename_pos == '"') filename_pos++;  // Skip quote if present
            
            char *end = strpbrk(filename_pos, "\";");
            if (end) *end = '\0';  // Terminate at the first quote or semicolon
            
            strncpy((char *)cls, filename_pos, 255);
        }
    }
    return MHD_YES;
}
*/

static enum MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                                            const char *url, const char *method,
                                            const char *version, const char *upload_data,
                                            size_t *upload_data_size, void **con_cls) {
        if (strcmp(method, "POST") == 0) {
        struct connection_info_struct *con_info = *con_cls;
        if (con_info == NULL) {
            con_info = malloc(sizeof(struct connection_info_struct));
            if (!con_info) {
                return MHD_NO;
            } // malloc error....

            con_info->filename = malloc(256);            
            if(!con_info->filename) {
                free(con_info);
                return MHD_NO;
            } // malloc error...
            con_info->filename[256] = '\0';
            con_info->fp = NULL;
			// Create a post processor
			con_info->post_processor = MHD_create_post_processor(connection, 1024,
                                                                  handle_file_upload,
                                                                  con_info);

            // Open file in binary mode
            /*
            con_info->fp = fopen(con_info->filename, "wb");
            if (con_info->fp == NULL) {
                free(con_info->filename);
                free(con_info);
                return MHD_NO;
            }
                */
            *con_cls = con_info;
            return MHD_YES;
        }

        // If there is data to upload, write it to the file
        if (*upload_data_size) {
			MHD_post_process(con_info->post_processor, upload_data, *upload_data_size);
			*upload_data_size = 0; // Reset upload size
            return MHD_YES;
        } else {
            
            if (con_info->fp == NULL) {
                const char *error_response = "{\"error\": \"No file uploaded\"}";
                struct MHD_Response *response_obj = MHD_create_response_from_buffer(strlen(error_response),
                                                                                    (void *)error_response,
                                                                                    MHD_RESPMEM_MUST_COPY);
                MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response_obj);
                MHD_destroy_response(response_obj);
                free(con_info->filename);
                free(con_info);
                return MHD_YES;
            }
            
            // When the upload is done, close the file and respond with the URL
            if (con_info->fp) fclose(con_info->fp);
            char response[512];
            const char *filename = strrchr(con_info->filename, '/');
            filename = (filename) ? filename + 1 : con_info->filename;  // Use full name if no '/'
            snprintf(response, sizeof(response), "{\"image_url\": \"%s/%s\"}",
                server_config.hostname,
                filename);
            free(con_info->filename);
            free(con_info);

            struct MHD_Response *response_obj = MHD_create_response_from_buffer(
                strlen(response),
                (void *)response,
                MHD_RESPMEM_MUST_COPY);
            MHD_queue_response(connection, MHD_HTTP_OK, response_obj);
            MHD_destroy_response(response_obj);
            return MHD_YES;
        }
    }

    return MHD_NO; // Only handle POST requests
}

void load_config(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return;  // Keep defaults

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    char *json_data = malloc(size + 1);
    fread(json_data, 1, size, file);
    json_data[size] = '\0';
    fclose(file);

    cJSON *json = cJSON_Parse(json_data);
    if (!json) return;

    cJSON *host = cJSON_GetObjectItem(json, "hostname");
    cJSON *port = cJSON_GetObjectItem(json, "port");
    cJSON *max_size = cJSON_GetObjectItem(json, "max_file_size");
    cJSON *dir = cJSON_GetObjectItem(json, "upload_dir");
    cJSON *thread_pool_size = cJSON_GetObjectItem(json, "thread_pool_size");
    cJSON *auto_filename = cJSON_GetObjectItem(json, "auto_generate_filename");

    if (cJSON_IsString(host)) strncpy(server_config.hostname, host->valuestring, sizeof(server_config.hostname));
    if (cJSON_IsNumber(port)) server_config.port = port->valueint;
    if (cJSON_IsNumber(max_size)) server_config.max_file_size = max_size->valueint;
    if (cJSON_IsString(dir)) strncpy(server_config.upload_dir, dir->valuestring, sizeof(server_config.upload_dir));
    if (cJSON_IsNumber(thread_pool_size)) server_config.thread_pool_size = thread_pool_size->valueint;
    if (auto_filename && cJSON_IsBool(auto_filename)) {
        server_config.auto_generate_filename = cJSON_IsTrue(auto_filename);
    }
    cJSON_Delete(json);
    free(json_data);
}

void print_config(Config config) {
    printf("Loaded Configuration:\n");
    printf("Hostname: %s\n", config.hostname);
    printf("Port: %d\n", config.port);
    printf("Max File Size: %zu\n", config.max_file_size);
    printf("Upload Directory: %s\n", config.upload_dir);
    printf("Thread Pool Size: %d\n", config.thread_pool_size);
    printf("Auto Generate Filename: %s\n", config.auto_generate_filename ? "true" : "false");
}

static struct MHD_Daemon *daemon;

void handle_signal(int sig) {
    printf("\nShutting down server...\n");

    if (daemon) {
        MHD_stop_daemon(daemon);
    }

    exit(0);
}

void handle_sighup(int sig) {
    load_config("image_server.conf");
    printf("Config reloaded.\n");
}


int main(int argc, char *argv[]) {

    // Default config file
    const char *config_file = "config.json";

    // Use provided config file if given
    if (argc > 1) {
        config_file = argv[1];
    }
    printf("Loading config from: %s\n", config_file);
    load_config(config_file);
    print_config(server_config);
    signal(SIGHUP, handle_sighup);
    signal(SIGINT, handle_signal);  // Ctrl + C
    signal(SIGTERM, handle_signal); // Kill command
    // Create the uploads directory if it doesn't exist
    mkdir(server_config.upload_dir, 0777);
    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, server_config.port, NULL, NULL,
                              &answer_to_connection, NULL,
                              MHD_OPTION_THREAD_POOL_SIZE, server_config.thread_pool_size, MHD_OPTION_END);
    if (daemon == NULL) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    printf("Image server running with %d threads on http://localhost:%d/\n",
        server_config.thread_pool_size,
        server_config.port);
    while (1) {
        pause(); // Wait indefinitely until a signal is received
    }
    return 0;
}
