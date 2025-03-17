#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <microhttpd.h>
#include <sys/stat.h>

#define PORT 8888
#define MAX_FILE_SIZE 10485760  // 10 MB
#define UPLOAD_DIR "./uploads"  // Directory to store uploaded images

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
    struct connection_info_struct *con_info = cls;

	if(filename)
	{			
		// Open the file if it hasn't been opened yet
		if (con_info->fp == NULL) {
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

static enum MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                                            const char *url, const char *method,
                                            const char *version, const char *upload_data,
                                            size_t *upload_data_size, void **con_cls) {
    if (strcmp(method, "POST") == 0) {
        struct connection_info_struct *con_info = *con_cls;

        if (con_info == NULL) {
            con_info = malloc(sizeof(struct connection_info_struct));
            con_info->filename = malloc(256);

            // Create a unique filename based on the current time
            sprintf(con_info->filename, UPLOAD_DIR "/uploaded_image_%ld.jpg", (long)time(NULL));

			// Create a post processor
			con_info->post_processor = MHD_create_post_processor(connection, 1024,
                                                                  handle_file_upload,
                                                                  con_info);

            // Open file in binary mode
            con_info->fp = fopen(con_info->filename, "wb");
            if (con_info->fp == NULL) {
                free(con_info->filename);
                free(con_info);
                return MHD_NO;
            }
            *con_cls = con_info;
            return MHD_YES;
        }

        // If there is data to upload, write it to the file
        if (*upload_data_size) {
			MHD_post_process(con_info->post_processor, upload_data, *upload_data_size);
			*upload_data_size = 0; // Reset upload size
            return MHD_YES;
        } else {
            // When the upload is done, close the file and respond with the URL
            fclose(con_info->fp);
            char response[512];
            snprintf(response, sizeof(response), "{\"filename\": \"%s\"}", strrchr(con_info->filename, '/') + 1);

            free(con_info->filename);
            free(con_info);

            struct MHD_Response *response_obj = MHD_create_response_from_buffer(strlen(response), (void *)response, MHD_RESPMEM_MUST_COPY);
            MHD_queue_response(connection, MHD_HTTP_OK, response_obj);
            MHD_destroy_response(response_obj);
            return MHD_YES;
        }
    }

    return MHD_NO; // Only handle POST requests
}

int main(void) {
    struct MHD_Daemon *daemon;

    // Create the uploads directory if it doesn't exist
    mkdir(UPLOAD_DIR, 0777);

    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
                              &answer_to_connection, NULL, MHD_OPTION_END);
    if (daemon == NULL) {
        return 1;
    }

    printf("Image server running on http://localhost:%d/\n", PORT);
    getchar(); // Wait for user input before shutting down

    MHD_stop_daemon(daemon);
    return 0;
}
