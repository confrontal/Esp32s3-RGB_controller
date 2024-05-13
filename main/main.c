#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spi_flash_mmap.h"
#include <esp_http_server.h>
#include "nvs_flash.h"
#include "esp_vfs.h"
//#include "esp_spiffs.h"
#include "esp_vfs_fat.h"

#include <sys/param.h>
#include <dirent.h>
#include "connect_wifi.h"
#include "cJSON/cJSON.h"
//#include "ws28xx/ws28xx.h"
#include "global_settings.h"

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_FATFS_MAX_LFN)


#define SCRATCH_BUFSIZE  8192

#define LED_PIN 1
#define RGB_GPIO 48
#define INIT_RGB_LEN 5
#define MAX_LENGTH_RGB_BUFFER 100
#define DEFAULT_RGB_VALUE_ON_STARTUP 0


g_Settings settings;

//CRGB *rgb_buffer;
//int rgb_buffer_size;

typedef enum Msg_Type{
    SYNC_E,     //info to set up site how others are currently using it
    LIGHTS_E,   //when another user number of lights
    INPUT_E,    //when another user changes input
    AFFECT_E    //if all LED's are controlled by one color picker
}Msg_Type;

httpd_handle_t server = NULL;
struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
    Msg_Type type;
    int data;
};

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

static const char *TAG = "WebSocket Server"; // TAG for debug
int led_state = 0;

#define INDEX_HTML_PATH "/spiflash/index.html"
#define BASE_PATH "/spiflash"
char index_html[4096];
char response_data[4096];


struct file_server_data {
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};
static void initi_web_page_buffer(void)
{
    /*  REGISTERING SPIFFS !!!DEPRICATED!!!
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
    */
   ESP_LOGI(TAG, "Mounting FAT filesystem");
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 5,
            .format_if_mount_failed = false,
            .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
            .disk_status_check_enable = false,
    };
    esp_err_t err;
    err = esp_vfs_fat_spiflash_mount_rw_wl(BASE_PATH, "storage", &mount_config, &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }

    memset((void *)index_html, 0, sizeof(index_html));
    struct stat st;
    if (stat(INDEX_HTML_PATH, &st))
    {
        ESP_LOGE(TAG, "index.html not found");
        return;
    }   
    FILE *fp = fopen(INDEX_HTML_PATH, "r");
    if (fread(index_html, st.st_size, 1, fp) == 0)
    {
        ESP_LOGE(TAG, "fread failed");
    }   
    fclose(fp);
}

/* Handler to redirect incoming GET request for /index.html to /
 * This can be overridden by uploading file with same name */
static esp_err_t index_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);  // Response body can be empty
    return ESP_OK;
}

static esp_err_t http_resp_dir_html(httpd_req_t *req){
    int response;

    sprintf(response_data, index_html);

    response = httpd_resp_send(req, response_data, HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG,"default website sent");
    return response;
}

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static const char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}
#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)
/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    } else if (IS_FILE_EXT(filename, ".css")){
        return httpd_resp_set_type(req,"text/css");
    } else if (IS_FILE_EXT(filename, ".js")){
        return httpd_resp_set_type(req,"text/javascript");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

static esp_err_t http_send_file(httpd_req_t *req){
    
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri, sizeof(filepath));
    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "URI REQUESTED: %s \tfilename: %s", req->uri,filename);

    /* If name has trailing '/', respond with directory contents */
    if (filename[strlen(filename) - 1] == '/') { // this also passes when visiting "http:IP/" since it also ends with a /
        ESP_LOGI(TAG,"default website requested");
        return http_resp_dir_html(req);//default visible website
    }

    ESP_LOGI(TAG,"p filepath: %p -> %s",filepath,filepath);
    

    if (stat(filepath, &file_stat) == -1) {
        /* If file not present on SPIFFS check if URI
         * corresponds to one of the hardcoded paths */
        if (strcmp(filename, "/index.html") == 0) {
            return index_html_get_handler(req);
        }
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    
    if(!fd){
        int res = fcloseall();
        if (res != 0){
            ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
            // Respond with 500 Internal Server Error 
            
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
            return ESP_FAIL;
        }

        fd = fopen(filepath, "r");

        if (!fd) {
            ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
            // Respond with 500 Internal Server Error 
            
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
            return ESP_FAIL;
        }
    }
    
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
               return ESP_FAIL;
           }
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);
    fflush(fd);
    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
#ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
    httpd_resp_set_hdr(req, "Connection", "close");
#endif
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;

}

esp_err_t get_req_handler(httpd_req_t *req)
{
    int response;

    sprintf(response_data, index_html);

    response = httpd_resp_send(req, response_data, HTTPD_RESP_USE_STRLEN);
    return response;
}

void addColorToMsg(cJSON *Object,char* label){
    cJSON *colors = cJSON_CreateArray();
    cJSON *color;
    cJSON *value;

    for(int i=0;i<settings.length_rgb_buffer;i++){
        color = cJSON_CreateArray();

        value = cJSON_CreateNumber(settings.rgb_buffer[i].r);
        cJSON_AddItemToArray(color,value);
        value = cJSON_CreateNumber(settings.rgb_buffer[i].g);
        cJSON_AddItemToArray(color,value);
        value = cJSON_CreateNumber(settings.rgb_buffer[i].b);
        cJSON_AddItemToArray(color,value);

        cJSON_AddItemToArray(colors,color);
    }
    cJSON_AddItemToObject(Object,label,colors);
}

char* constructMSG(Msg_Type type, int data){
    char* jsonStr = NULL;
    cJSON *message = cJSON_CreateObject();//whole object

    ESP_LOGI(TAG, "Constructing message: type(%d)",(int)type);

    switch(type){
        default:
        case SYNC_E:
            cJSON *sync = cJSON_CreateObject();
            cJSON_AddItemToObject(message,"sync",sync);

            cJSON *applyAll = cJSON_CreateBool(settings.apply_all);
            cJSON_AddItemToObject(sync,"affect_all",applyAll);

            cJSON *length = cJSON_CreateNumber(settings.length_rgb_buffer);
            cJSON_AddItemToObject(sync,"length",length);

            addColorToMsg(sync,"colors");
            
            break;
        case LIGHTS_E://"{"resized":{"length":2,"colors":[[0,0,0],[0,0,0],[255,255,0],[60,80,50]]}}"
            cJSON *resized = cJSON_CreateObject();
            cJSON_AddItemToObject(message,"resized",resized);

            cJSON *l = cJSON_CreateNumber(settings.length_rgb_buffer);
            cJSON_AddItemToObject(resized,"length",l);

            addColorToMsg(resized,"colors");
            break;
        case AFFECT_E:
            cJSON *affect_all = cJSON_CreateBool(settings.apply_all);
            cJSON_AddItemToObject(message,"affect_all",affect_all);
            break;
        case INPUT_E:
            cJSON *input = NULL;
            cJSON *affected = NULL;
            cJSON *color = NULL;
            cJSON *c_value = NULL;

            input = cJSON_CreateObject();
            cJSON_AddItemToObject(message,"input",input);

            affected = cJSON_CreateNumber(data);//can only change global value
            cJSON_AddItemToObject(input,"affected",affected);

            color = cJSON_CreateArray();

            if(data == (-1)){
                c_value = cJSON_CreateNumber(settings.rgb_buffer[0].r);
                cJSON_AddItemToArray(color,c_value);
                c_value = cJSON_CreateNumber(settings.rgb_buffer[0].g);
                cJSON_AddItemToArray(color,c_value);
                c_value = cJSON_CreateNumber(settings.rgb_buffer[0].b);
                cJSON_AddItemToArray(color,c_value);
            }else{
                c_value = cJSON_CreateNumber(settings.rgb_buffer[data].r);
                cJSON_AddItemToArray(color,c_value);
                c_value = cJSON_CreateNumber(settings.rgb_buffer[data].g);
                cJSON_AddItemToArray(color,c_value);
                c_value = cJSON_CreateNumber(settings.rgb_buffer[data].b);
                cJSON_AddItemToArray(color,c_value);
            }

            cJSON_AddItemToObject(input,"color",color);
            break;
    }

    jsonStr = cJSON_PrintUnformatted(message);
    ESP_LOGI(TAG,"Message constructed: type:%d -> %s",(int)type,jsonStr);
    if (jsonStr == NULL)
    {
        ESP_LOGE(TAG,"Failed to write json message");
        cJSON_Delete(message);
        return NULL;
    }
    cJSON_Delete(message);
    return jsonStr;
}

static void ws_async_send(void *arg)
{
    httpd_ws_frame_t ws_pkt;
    struct async_resp_arg *resp_arg = arg;
    httpd_handle_t hd = resp_arg->hd;
    Msg_Type msg_type_to_send = resp_arg->type;
    int msg_data = resp_arg->data;
    //int fd = resp_arg->fd;

    //led_state = !led_state;
    gpio_set_level(LED_PIN, led_state);
    
    //example //{"input":{"affected":-1,"color":{0,0,0}}}
    char *json_string = constructMSG(msg_type_to_send,msg_data);
    
    
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)json_string;
    ws_pkt.len = strlen(json_string);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    static size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;
    int client_fds[max_clients];

    esp_err_t ret = httpd_get_client_list(server, &fds, client_fds);

    if (ret != ESP_OK) {
        free(json_string);
        return;
    }

    //send message to every client
    for (int i = 0; i < fds; i++) {
        int client_info = httpd_ws_get_fd_info(server, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(hd, client_fds[i], &ws_pkt);
        }
    }
    free(resp_arg);
    free(json_string);
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req,Msg_Type type, int data)
{
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    resp_arg->type = type;
    resp_arg->data = data;
    return httpd_queue_work(handle, ws_async_send, resp_arg);
}

static esp_err_t handle_ws_req(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, a new connection was opened");

        trigger_async_send(req->handle, req,SYNC_E,0);//if new client send current information

        return ESP_OK;
    }
    Msg_Type messageType = SYNC_E;
    int msg_data = 0;
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len)
    {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }

    ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);

    //implement parsing of information

    cJSON *json = cJSON_Parse((char *)ws_pkt.payload);
    if(json == NULL){
        const char *error_ptr = cJSON_GetErrorPtr(); 
        if (error_ptr != NULL) { 
            ESP_LOGE(TAG,"Error: %s\n", error_ptr); 
        } 
        cJSON_Delete(json); 
        free(buf);
        return ESP_FAIL;
    }

    //example {"lights":{"affected":-1,"color":{"r":255,"g":255,"b":255}}}
    cJSON *length = cJSON_GetObjectItemCaseSensitive(json,"length");
    cJSON *lights = cJSON_GetObjectItemCaseSensitive(json,"lights");
    cJSON *affectall = cJSON_GetObjectItemCaseSensitive(json,"affectall");
    


    if(length != NULL){//resize CRGB array

        int new_Length = length->valueint;
        new_Length = new_Length > 0 ? (new_Length % (settings.max_length_rgb_buffer+1)) : 1;//sanatize user input
        esp_err_t err = ESP_OK;


        CRGB *rgb_buffer = settings.rgb_buffer;

        if(new_Length == settings.length_rgb_buffer){
            cJSON_Delete(json);
            free(buf);
            return err;
        }

        //create temporary duplucate of current light settings
        CRGB* tmp_buff = calloc(sizeof(CRGB),settings.length_rgb_buffer);
        if(tmp_buff == NULL){
            cJSON_Delete(json); 
            free(buf);
            return ESP_FAIL;
        }

        for(int i=0; i<settings.length_rgb_buffer;i++){
            tmp_buff[i] = rgb_buffer[i];
            if( i >= new_Length){
                rgb_buffer[i] = (CRGB){.r=0,.g=0,.b=0};
            }
            
        }
        if(settings.length_rgb_buffer > new_Length){
            ESP_ERROR_CHECK_WITHOUT_ABORT(ws28xx_update());//clear unadressable LED's
        }
        
        err = ws28xx_resize(new_Length);
        if(err != ESP_OK){ 
            ESP_LOGI(TAG,"[LENGTH_ADJ]: ERROR");
            cJSON_Delete(json);
            free(tmp_buff);
            free(buf);
            return err;
        }
        //transfer old values to new array
        for (int i = 0; i < new_Length; i++)
        {
            if(i>=settings.length_rgb_buffer){
                rgb_buffer[i] = (CRGB){.r=0,.g=0,.b=0};
                //ESP_LOGI(TAG,"[LENGTH_ADJ]: loaded black %d {.r=%d,.g=%d,.b=%d}",i,rgb_buffer[i].r,rgb_buffer[i].g,rgb_buffer[i].b);
            }
            else{
                rgb_buffer[i] = tmp_buff[i];
                //ESP_LOGI(TAG,"[LENGTH_ADJ]: loaded old value %d {.r=%d,.g=%d,.b=%d}",i,rgb_buffer[i].r,rgb_buffer[i].g,rgb_buffer[i].b);
            }
        }
        
        settings.length_rgb_buffer = new_Length;
        //ESP_LOGI(TAG,"[LENGTH_ADJ]: settings.length_rgb_buffer = %d",settings.length_rgb_buffer);
        free(tmp_buff);

        ESP_ERROR_CHECK_WITHOUT_ABORT(ws28xx_update());

        //send whole array over to clients now
        messageType = LIGHTS_E;
        msg_data = settings.length_rgb_buffer;
    }


    if(lights != NULL){
        cJSON *affected = cJSON_GetObjectItemCaseSensitive(lights,"affected");
        cJSON *color = cJSON_GetObjectItemCaseSensitive(lights,"color");
        cJSON *red = cJSON_GetObjectItemCaseSensitive(color,"r");
        cJSON *green = cJSON_GetObjectItemCaseSensitive(color,"g");
        cJSON *blue = cJSON_GetObjectItemCaseSensitive(color,"b");

        if (!cJSON_IsNumber(affected) || !cJSON_IsNumber(red) || !cJSON_IsNumber(green)|| !cJSON_IsNumber(blue))
        {
            cJSON_Delete(json);
            free(buf);
            return ESP_FAIL;
        }

        //ESP_LOGI(TAG,"JSON affected received: %d", affected->valueint);
        if(affected->valueint < 0){
            ws28xx_fill_all((CRGB){.r=(uint8_t)red->valueint,.g=(uint8_t)green->valueint,.b=(uint8_t)blue->valueint});
            ESP_LOGI(TAG,"All lights set {index: %d r:%d g:%d b:%d}",affected->valueint,settings.rgb_buffer[0].r,settings.rgb_buffer[0].g,settings.rgb_buffer[0].b);
        }else{
            int i = affected->valueint % settings.length_rgb_buffer;
            settings.rgb_buffer[i].r = (uint8_t)red->valueint;
            settings.rgb_buffer[i].g = (uint8_t)green->valueint;
            settings.rgb_buffer[i].b = (uint8_t)blue->valueint;

            ESP_LOGI(TAG,"light set {index: %d r:%d g:%d b:%d}",affected->valueint,settings.rgb_buffer[i].r,settings.rgb_buffer[i].g,settings.rgb_buffer[i].b);
        }
        

        ESP_ERROR_CHECK_WITHOUT_ABORT(ws28xx_update());
        messageType = INPUT_E;
        msg_data = affected->valueint;
    }

    if(affectall != NULL){
        bool affect = affectall->valueint == 1 ? true : false;

        if(settings.apply_all == affect){
            cJSON_Delete(json);
            free(buf);
            return ESP_OK;
        }
        settings.apply_all = affect;

        messageType = AFFECT_E;
        msg_data = settings.apply_all;
    }

    led_state = !led_state;
    cJSON_Delete(json);
    free(buf);
    return trigger_async_send(req->handle, req, messageType,msg_data);
}

httpd_handle_t setup_websocket_server(void)
{
    const char base_path[] = BASE_PATH;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    static struct file_server_data *server_data = NULL;
    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));

    strlcpy(server_data->base_path, base_path,
            sizeof(server_data->base_path));

    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_uri_t uri_get = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = http_send_file,
        .user_ctx = server_data};

    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = handle_ws_req,
        .user_ctx = NULL,
        .is_websocket = true};

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &ws);
        httpd_register_uri_handler(server, &uri_get);
    }

    return server;
}

void app_main()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    connect_wifi(false);
    

    if (wifi_connect_status)
    {
        esp_rom_gpio_pad_select_gpio(LED_PIN);
        gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

        led_state = 0;
        ESP_LOGI(TAG, "ESP32 ESP-IDF WebSocket Web Server is running ... ...\n");

        settings = (g_Settings){.rgb_buffer=NULL,.length_rgb_buffer=0,.apply_all=0};

        settings.length_rgb_buffer = INIT_RGB_LEN;
        settings.max_length_rgb_buffer = MAX_LENGTH_RGB_BUFFER;
        ESP_ERROR_CHECK_WITHOUT_ABORT(ws28xx_init(RGB_GPIO,WS2815,settings.max_length_rgb_buffer,&settings.rgb_buffer));
        ESP_ERROR_CHECK_WITHOUT_ABORT(ws28xx_resize(settings.length_rgb_buffer));
        initi_web_page_buffer();
        setup_websocket_server();
    }
}