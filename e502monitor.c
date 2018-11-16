// For work with Lcard E-502 device
#include "e502api.h"
// --------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

#include <libconfig.h>

// #define FILE_TIME 900 // create 15 minutes files... 15 min = 900 sec
#define FILE_TIME 60

#define TCP_CONNECTION_TOUT 5000

#define READ_CONFIG_OK 0 
#define READ_CONFIG_ERROR -1

#define CONFIGURE_OK 0
#define CONFIGURE_ERROR -2

#define WRITE_HEADERS_OK 0
#define WRITE_HEADERS_ERROR -3

// The structure that stores the startup parameters
typedef struct {
        int channel_count;      // Count of use logical chnnels
        double adc_freq;        // Frequency descritization
        int read_block_size;    // The size of the data block 
                                //  that is read at once from the ADC
        
        int read_timeout;       // Timeout for receiving data in ms.
        int *channel_numbers;   // Numbers of using logical channels
        int *channel_modes;     // Operation modes for channels
        int *channel_ranges;    // Channel measurement range
        char *bin_dir;          // Directory of output bin files
    } monitor_config;

// The structure that stores header for output bin files
typedef struct {
        uint16_t year;               // Year of creating bin file
        uint16_t month;              // Month of creating bin file
        uint16_t day;                // Day of start of the recording
        uint16_t start_hour;         // Hour of start of the recording
        uint16_t finish_hour;        // Hour of finish of the recording
        uint16_t start_minut;        // Minut of start of the recording
        uint16_t finish_minut;       // Minut of finish of the reocording
        uint16_t start_second;       // Second of start of the recording
        uint16_t finish_second;      // Second of finish of the recording
        uint16_t start_msecond;      // Milliseconds of start recording
        uint16_t finish_msecond;     // Milliseconds of finish recording
        uint16_t channel_number;     // Number of used channel 
        double   freq;               // Frequancy of ADC
        uint8_t  mode;               // Operation mode 
    } header;

// global variables
char    g_buffer_index; // index of use buffer
FILE**  g_files;       // output files
int     g_channel_count;      
int     g_read_block_size; 
double  g_adc_freq;
double* g_data;
double* g_data_bufer0;
double* g_data_buffer1;



// TODO: delete this!-------------------//
                                        //
// Global variable for the correct      //
// completion of data collection        //
int g_stop = 0;                         //
                                        //
// Signal of completion                 //
void abort_handler(int sig)             //
{                                       //
    g_stop = 1;                         //
}                                       //
//--------------------------------------//

void close_files()
{
    for(int i = 0; i < g_channel_count; ++i)
    {
        fclose(g_files[i]);
    }
}

void *write_data(void *arg)
{
    // char current_index = buffer_index;
    double* current_buffer = g_data;
    int ch_counter, data_counter;
    int data_count_in_file = FILE_TIME * g_adc_freq;
    int data_count = 0;
    while(!g_stop)
    {   
        if(current_buffer != g_data)
        {
            current_buffer = g_data;
            
            for(data_counter = 0; data_counter < g_read_block_size; data_counter += g_channel_count)
            {
                for(ch_counter = 0; ch_counter < g_channel_count; ++ch_counter)
                {
                    fwrite(&g_data[data_counter + ch_counter], sizeof(double), 1, g_files[ch_counter]);
                }
            }

            data_count += g_read_block_size;

            if( data_count > data_count_in_file ){ close_files(); g_stop = 1; }
        }
    }
}

/*
    Get all Lcard E-502 devices connected via USB 
    or Ethernet interfaces

    Return number of found devices 
*/
uint32_t get_all_devrec(t_x502_devrec **pdevrec_list,
                        uint32_t *ip_addr_list,
                        unsigned ip_cnt)
{
    // number of found devices
    int32_t fnd_devcnt = 0;
    // number of found devices connected via USB interface
    uint32_t usb_devcnt = 0;

    t_x502_devrec *rec_list = NULL;

    // get count of connected devices via USB interface
    E502_UsbGetDevRecordsList(NULL, 0, 0, &usb_devcnt);

    if(usb_devcnt + ip_cnt == 0){ return 0; }

    // allocate memory for the array to save the number of records found
    rec_list = malloc((usb_devcnt + ip_cnt) * sizeof(t_x502_devrec));
    
    // if memory wasn't allocated
    if(rec_list == NULL){ return 0; }

    if (usb_devcnt != 0) 
    {
        int32_t res = E502_UsbGetDevRecordsList(&rec_list[fnd_devcnt],
                                                usb_devcnt, 
                                                0, 
                                                NULL);
        if (res >= 0) { fnd_devcnt += res; }
    }
    
    for (int i=0; i < ip_cnt; i++) 
    {
        if (E502_MakeDevRecordByIpAddr(&rec_list[fnd_devcnt],
                                       ip_addr_list[i],
                                       0,
                                       TCP_CONNECTION_TOUT)
            == X502_ERR_OK // E502_MakeDevRecordByIpAddr(...) == X502_ERR_OK 
            ) 
        {
            fnd_devcnt++;
        }
    }

    // if were some mistake and no one modele
    // was getting
    if(fnd_devcnt == 0)
    {
        free(rec_list);
        return 0;
    }
    // --------------------------------------

    *pdevrec_list = rec_list;

    return fnd_devcnt;
}

/*
    Create connecting for selecting device

    Return special handler (t_x502_hnd)
*/
t_x502_hnd open_device( t_x502_devrec *devrec_list,
                        uint32_t device_id )
{
    t_x502_hnd hnd = X502_Create();

    if (hnd == NULL) 
    {
        fprintf(stderr, "Ошибка создания описателя модуля!");
        return hnd;
    }
    
    /* create connection with module */
    int32_t err = X502_OpenByDevRecord(hnd, &devrec_list[device_id]);
    if (err != X502_ERR_OK) 
    {
        fprintf(stderr,
                "Ошибка установления связи с модулем: %s!",
                X502_GetErrorString(err));

        X502_Free(hnd);
        hnd = NULL;
        return hnd;
    }

    return hnd;
}

void create_stop_event_handler()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = abort_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

/* 
    Print info about module 

    Return error index
*/  
uint32_t print_info_about_module(t_x502_hnd *hnd)
{
    t_x502_info info;
    uint32_t err = X502_GetDevInfo(hnd, &info);
    if (err != X502_ERR_OK) {
        fprintf(stderr, 
                "Ошибка получения серийного информации о модуле: %s!",
                 X502_GetErrorString(err)
                 );
        return err;
    } 

    printf("Установлена связь со следующим модулем:\n");
    printf(" Серийный номер\t\t\t: %s\n", info.serial);
    
    printf(" Наличие ЦАП\t\t\t: %s\n",
           info.devflags & X502_DEVFLAGS_DAC_PRESENT ? "Да" : "Нет");
    
    printf(" Наличие BlackFin\t\t: %s\n",
           info.devflags & X502_DEVFLAGS_BF_PRESENT ? "Да" : "Нет");
    
    printf(" Наличие гальваноразвязки\t: %s\n",
           info.devflags & X502_DEVFLAGS_GAL_PRESENT ? "Да" : "Нет");
    
    printf(" Индустриальное исп.\t\t: %s\n",
           info.devflags & X502_DEVFLAGS_INDUSTRIAL ? "Да" : "Нет");
    
    printf(" Наличие интерф. PCI/PCIe\t: %s\n",
           info.devflags & X502_DEVFLAGS_IFACE_SUPPORT_PCI ? "Да" : "Нет");
    
    printf(" Наличие интерф. USB\t\t: %s\n",
           info.devflags & X502_DEVFLAGS_IFACE_SUPPORT_USB ? "Да" : "Нет");
    
    printf(" Наличие интерф. Ethernet\t: %s\n",
           info.devflags & X502_DEVFLAGS_IFACE_SUPPORT_ETH ? "Да" : "Нет");
    
    printf(" Версия ПЛИС\t\t\t: %d.%d\n",
           (info.fpga_ver >> 8) & 0xFF,
            info.fpga_ver & 0xFF);
    printf(" Версия PLDA\t\t\t: %d\n", info.plda_ver);
    
    if (info.mcu_firmware_ver != 0) {
        printf(" Версия прошивки ARM\t\t: %d.%d.%d.%d\n",
               (info.mcu_firmware_ver >> 24) & 0xFF,
               (info.mcu_firmware_ver >> 16) & 0xFF,
               (info.mcu_firmware_ver >>  8) & 0xFF,
                info.mcu_firmware_ver & 0xFF);
    }

    return err;
}

/*
    Create config for module. 

    mc - pointer to config structure 

    Return error index
*/
int create_config(monitor_config *mc)
{
    config_t cfg; 

    config_init(&cfg);

    if(!config_read_file(&cfg, "e502monitor.cfg"))
    {
        fprintf(stderr, "%s:%d - %s\n",  config_error_file(&cfg),
                config_error_line(&cfg), config_error_text(&cfg));
        config_destroy(&cfg);

        return READ_CONFIG_ERROR;
    }

    int found_value;
    int err = config_lookup_int(&cfg, "channel_count", &mc->channel_count);
    if(err == CONFIG_FALSE)
    { 
        printf("Ошибка конфигурационного файла:\tколичество каналов не задано!");
        config_destroy(&cfg);
        return READ_CONFIG_ERROR;
    }

    err = config_lookup_float(&cfg, "adc_freq", &(mc->adc_freq));
    if(err == CONFIG_FALSE)
    { 
        printf("Ошибка конфигурационного файла:\tчастота сбора АЦП не задана!");
        config_destroy(&cfg);
        return READ_CONFIG_ERROR;
    }
    
    err = config_lookup_int(&cfg, "read_block_size", &(mc->read_block_size));
    if(err == CONFIG_FALSE)
    { 
        printf("Ошибка конфигурационного файла:\tразмер блока для чтения не задан!");
        config_destroy(&cfg);
        return READ_CONFIG_ERROR;
    }
    
    err = config_lookup_int(&cfg, "read_timeout", &(mc->read_timeout));
    if(err == CONFIG_FALSE)
    { 
        printf("Ошибка конфигурационного файла:\tвремя задержки перед чтением блока не задано!");
        config_destroy(&cfg);
        return READ_CONFIG_ERROR; 
    }

    config_setting_t *channel_numbers = config_lookup(&cfg, "channel_numbers");
    if(channel_numbers == NULL)
    {
        printf("Ошибка конфигурационного файла:\tномера каналов не заданы!");
        config_destroy(&cfg);
        return READ_CONFIG_ERROR;
    }

    mc->channel_numbers = (int*)malloc(sizeof(int)*mc->channel_count);

    for(int i = 0; i < mc->channel_count; i++)
    {
        mc->channel_numbers[i] = config_setting_get_int_elem(channel_numbers, i);
    }

    config_setting_t *channel_modes = config_lookup(&cfg, "channel_modes");
    if(channel_modes == NULL)
    {
        printf("Ошибка конфигурационного файла:\tрежимы каналов не заданы!");
        config_destroy(&cfg);
        return READ_CONFIG_ERROR;
    }

    mc->channel_modes = (int*)malloc(sizeof(int)*mc->channel_count);
    
    for(int i = 0; i < mc->channel_count; i++)
    {
        mc->channel_modes[i] = config_setting_get_int_elem(channel_modes, i);
    }

    config_setting_t *channel_ranges = config_lookup(&cfg, "channel_ranges");
    if(channel_ranges == NULL)
    {
        printf("Ошибка конфигурационного файла:\tдиапазоны каналов не заданы!");
        config_destroy(&cfg);
        return READ_CONFIG_ERROR;
    }

    mc->channel_ranges = (int*)malloc(sizeof(int)*mc->channel_count);

    for(int i = 0; i < mc->channel_count; i++)
    {
        mc->channel_ranges[i] = config_setting_get_int_elem(channel_ranges, i);
    }

    config_destroy(&cfg);

    return READ_CONFIG_OK;
}

void print_config(monitor_config *mc)
{
    printf("\nЗагружена следующая конфигурация модуля:\n");
    printf(" Количество используемых логических каналов\t\t:%d\n", mc->channel_count);
    printf(" Частота сбора АЦП в Гц\t\t\t\t\t:%f\n", mc->adc_freq);
    printf(" Количество отсчетов, считываемых за блок\t\t:%d\n", mc->read_block_size);
    printf(" Таймаут перед считываением блока (мс)\t\t\t:%d\n", mc->read_timeout);
    
    printf(" Номера используемых каналов\t\t\t\t:[ ");
    for(int i = 0; i<mc->channel_count; i++)
    {
        printf("%d ", mc->channel_numbers[i]);
    }
    printf("]\n");

    printf(" Режимы измерения каналов\t\t\t\t:[ ");
    for(int i = 0; i<mc->channel_count; i++)
    {
        printf("%d ", mc->channel_modes[i]);
    }
    printf("]\n");

    printf(" Диапазоны измерения каналов\t\t\t\t:[ ");
    for(int i = 0; i<mc->channel_count; i++)
    {
        printf("%d ", mc->channel_ranges[i]);
    }
    printf("]\n");
}

uint32_t configure_module(t_x502_hnd *hnd,
                          monitor_config *mc)
{
    int32_t err = X502_SetLChannelCount(hnd, mc->channel_count);

    if(err != X502_ERR_OK){ return CONFIGURE_ERROR; }

    for(int i = 0; i < mc->channel_count; ++i)
    {
        err = X502_SetLChannel( hnd,
                                i,
                                mc->channel_numbers[i],
                                mc->channel_modes[i],
                                mc->channel_ranges[i],
                                0 );

        if(err != X502_ERR_OK){ return CONFIGURE_ERROR; }
    }

    g_adc_freq = mc->adc_freq;
    double frame_freq = mc->adc_freq/mc->channel_count;

    err = X502_SetAdcFreq(hnd, &g_adc_freq, &frame_freq);
    if(err != X502_ERR_OK){ return CONFIGURE_ERROR; }

    //what we realy set...
    printf("\nУстановлены частоты:\n"
           " Частота сбора АЦП\t\t:%0.0f\n"
           " Частота на лог. канал\t\t:%0.0f\n",
           g_adc_freq, frame_freq);

    err = X502_Configure(hnd, 0);
    if(err != X502_ERR_OK){ return CONFIGURE_ERROR; }

    err = X502_StreamsEnable(hnd, X502_STREAM_ADC);
    if(err != X502_ERR_OK){ return CONFIGURE_ERROR; }

    return CONFIGURE_OK;

}

int create_files(FILE** files, header *hs, int size)
{
    int i; // counter

    for(i = 0; i < size; ++i)
    {

    }
}

uint32_t write_file_headers(FILE** files, monitor_config *mc)
{    // try open files for writing
    for(int i = 0; i<mc->channel_count; i++)
    {
        char c[2];
        sprintf(c, "%d", i);
        char file_name[8] = "data"; 
        strcat(file_name, c);

        files[i] = fopen(file_name, "wb");

        if(files[i] == NULL){ 
            for(int j = 0; j < mc->channel_count; j++)
            {
                if(files[j] != NULL){ fclose(files[j]); }
            }
            return WRITE_HEADERS_ERROR;
        }

        // fwrite( &(mc->channel_numbers[i]), sizeof(uint), 1, files[i] );
        // fwrite( &(mc->adc_freq), sizeof(double), 1, files[i] );
        // fwrite( &(mc->channel_modes[i]), sizeof(uint), 1, files[i] );
        // fwrite( &(mc->channel_ranges[i]), sizeof(uint), 1, files[i] );

        // fclose(files[i]);

        // files[i] = fopen(file_name, "rb");

        // uint cn, cm, cr;
        // double af, df;

        // fread(&cn, sizeof(uint), 1, files[i]);
        // fread(&af, sizeof(double), 1, files[i]);
        // fread(&df, sizeof(double), 1, files[i]);
        // fread(&cm, sizeof(uint), 1, files[i]);
        // fread(&cr, sizeof(uint), 1, files[i]);

        // printf("Cannel numbers = %d\n", cn);
        // printf("ADC FREQ = %f\n", af);
        // printf("DIN FREQ = %f\n", df);
        // printf("Channel modes = %d\n", cm);
        // printf("Channel ranges = %d\n", cr);

        // fclose(files[i]);
    }

    return WRITE_HEADERS_OK;
}

void write_data_in_file( FILE *file,
                         double *data,
                         uint channel_count )
{

}

void close_file_streams( FILE **files, int size )
{
    for(int i = 0; i<size; i++)
    {
        fclose(files[i]);
    }
}

int main(int argc, char** argv) 
{
    create_stop_event_handler();

    uint32_t ver = X502_GetLibraryVersion();
    printf("Версия библиотеки: %d.%d.%d\n",
            (ver >> 24)&0xFF,
            (ver>>16)&0xFF,
            (ver>>8)&0xFF
          );

    uint32_t *ip_addr_list = NULL;
    uint32_t ip_cnt = 0;
    uint32_t fnd_devcnt = 0;

    t_x502_devrec *devrec_list = NULL;

    fnd_devcnt = get_all_devrec(&devrec_list, ip_addr_list, ip_cnt);

    if (fnd_devcnt == 0) {
        printf("Не найдено ни одного модуля. Завершение.\n");
        
        //exit from program
        return 0;
    }
    
    printf("Доступны следующие модули:\n");
    for (int i=0; i < fnd_devcnt; i++) {
        printf("Module № %d: %s, %-9s", i, devrec_list[i].devname,
               devrec_list[i].iface == X502_IFACE_PCI ? "PCI/PCIe" :
               devrec_list[i].iface == X502_IFACE_USB ? "USB" :
               devrec_list[i].iface == X502_IFACE_ETH ? "Ethernet" : "?");
        if (devrec_list[i].iface != X502_IFACE_ETH) {
            printf("Серийные номер: %s\n", devrec_list[i].serial);
        } else {
            printf("Адрес: %s\n", devrec_list[i].location);
        }
    }

    printf("Введите номер модуля, с которым хотите работать (от 0 до %d)\n", fnd_devcnt-1);
    fflush(stdout);

    uint32_t device_id = -1;

    scanf("%d", &device_id);

    if( device_id < 0 || device_id >= fnd_devcnt)
    {
        printf("\nНеверный номер модуля! Завершение.\n");

        // Free memory
        X502_FreeDevRecordList(devrec_list, fnd_devcnt);
        free(devrec_list);
        // --------------------------------------------

        return 0; // exit from program
    }

    t_x502_hnd hnd = open_device(devrec_list, device_id);

    if(hnd == NULL)
    {
        // Free memory
        X502_FreeDevRecordList(devrec_list, fnd_devcnt);
        free(devrec_list);
        // --------------------------------------------

        return 0;
    }

    print_info_about_module(hnd);

    monitor_config *mc = (monitor_config*)malloc(sizeof(monitor_config));
    mc->channel_numbers = NULL;
    mc->channel_modes   = NULL;
    mc->channel_ranges  = NULL;

    if(create_config(mc) == READ_CONFIG_ERROR)
    {
        if(mc->channel_numbers != NULL){ free(mc->channel_numbers); }
        if(mc->channel_modes   != NULL){ free(mc->channel_modes); }
        if(mc->channel_ranges  != NULL){ free(mc->channel_ranges); }

        free(mc);
        
        X502_Close(hnd);
        X502_Free(hnd);

        printf("\nОшибка при создании конфигурации модуля! Заверешение.\n");

        return 0;
    }
    
    print_config(mc);

    if( configure_module(hnd, mc) != CONFIGURE_OK )
    {
        printf("\nОшибка при конфигурировании модуля! Завершение.\n");

        free(mc->channel_numbers);
        free(mc->channel_ranges);
        free(mc->channel_modes);

        free(mc);

        X502_Close(hnd);
        X502_Free(hnd);

        return 0;
    }
    int read_time_out   = mc->read_timeout;
    g_read_block_size = mc->read_block_size;
    g_channel_count   = mc->channel_count;
    double freq         = mc->adc_freq;
    
    uint32_t err = X502_StreamsStart(hnd);
    if(err != X502_ERR_OK)
    {
        fprintf(stderr,
                "Ошибка запуска сбора данных: %s!\n",
                X502_GetErrorString(err)
                );
        X502_Close(hnd);
        X502_Free(hnd);

        return 0;
    }

    printf("Сбор данных запущен. Для остановки нажмите Ctrl+C\n");
    fflush(stdout);

    uint32_t* rcv_buf  = (uint32_t*)malloc(sizeof(uint32_t)*g_read_block_size);
    
    int32_t  rcv_size;
    uint32_t adc_size;
    uint32_t first_lch;



    g_files = (FILE*)malloc(sizeof(FILE*)*g_channel_count);

    if( write_file_headers(out_files, mc) != WRITE_HEADERS_OK )
    {
        printf("Ошибка записи заголовков выходных файлов! Завершение.\n");

        free(mc->channel_numbers);
        free(mc->channel_ranges);
        free(mc->channel_modes);

        free(mc);

        free(rcv_buf);

        free(out_files);

        X502_Close(hnd);
        X502_Free(hnd);

        return 0; // exit from program
    }

    // create file headers
    header *hs = (header*)malloc(sizeof(header)*channel_count);

    for(int i = 0; i < channel_count; i++)
    {
        
    }


    /* 
        Two buffers for reading data. While one of them is filled
        data from other writing to binary files.
    */
    g_data_buffer0 = (double*)malloc(sizeof(double)*read_block_size);
    g_data_buffer1 = (double*)malloc(sizeof(double)*read_block_size);

    g_buffer_index = 0; // index of current buffer
    
    // pointer to current buffer 
    // equals data_buffer0 or data_buffer1
    g_data = g_data_buffer0;


    while(!g_stop)
    {
        // switch between buffers
        g_data = (g_buffer_index == 0) ? g_data_buffer0 : g_data_buffer1;

        rcv_size = X502_Recv(hnd, rcv_buf, read_block_size, read_time_out);

        if(rcv_size <= 0)
        {
            fprintf(stderr, "Ошибка приема данных: %s\n", X502_GetErrorString(err));
            g_stop = 1;
            continue;
        }

        X502_GetNextExpectedLchNum(hnd, &first_lch);

        // is it right?
        adc_size = sizeof(double)*read_block_size;

        err = X502_ProcessData(hnd, rcv_buf, rcv_size, X502_PROC_FLAGS_VOLT,
                               g_data, &adc_size, NULL, NULL);

        if(err != X502_ERR_OK)
        {
            fprintf(stderr,
                    "Ошибка обработки данных: %s\n",
                    X502_GetErrorString(err)
                   );
            g_stop = 1;
            continue;
        }

        // change buffer index
        g_buffer_index = ( g_buffer_index == 1 ) ? 0 : 1;

        // for(value_count = 0; value_count < adc_size; value_count += channel_count)
        // {
            
        //     for(lch_count = 0; lch_count < channel_count; lch_count++)
        //     {   
        //         fwrite(&adc_data[value_count + lch_count],
        //                sizeof(double),
        //                1,out_files[lch_count] 
        //               );
        //     }
        // }

        g_stop = 1;
    }

    if( X502_StreamsStop(hnd) != X502_ERR_OK)
    {
        fprintf(stderr, "Ошибка останова сбора данных!\n");
    } else 
    {
        printf("Сбор данных остановлен успешно\n");
    }

    close_file_streams(out_files, channel_count);

    // Free all memory...
    free(g_files);

    free(mc->channel_numbers);
    free(mc->channel_ranges);
    free(mc->channel_modes);

    free(mc);

    free(rcv_buf);

    free(hs);

    free(g_data_buffer0);
    free(g_data_buffer1);

    X502_Close(hnd);
    X502_Free(hnd);

    return 0;
}

