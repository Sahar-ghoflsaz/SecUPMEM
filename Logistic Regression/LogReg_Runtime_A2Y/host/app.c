/**
* app.c
* LogReg_1.1 Host Application Source File
* int32 and float 
* sigmoid simulated by Taylor-Series 
* 
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dpu.h>
#include <dpu_log.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <omp.h>
#include <math.h>
#include <time.h>

#include "../support/common.h"
#include "../support/timer.h"
#include "../support/params.h"
#include "../support/aes.h"
// Define the DPU Binary path as DPU_BINARY here
#ifndef DPU_BINARY
#define DPU_BINARY "./bin/dpu_code"
#endif

#define MAXCHAR 500

#if ENERGY
#include <dpu_probe.h>
#endif

#define PART 640
#define PART2 640
// Pointer declaration
static T* X;
static T* Y;
static T* W;
// static float* W;

// Create input arrays
static void read_input_float(float* X, float* Y, float* W, unsigned int m_size, unsigned int n_size) {
    srand(0);

    printf("Predefined weight: ");
    for (unsigned int w = 0; w < n_size; ++w) {
        W[w] = (T) (w+1); 
        // W[w] = (T) (rand()%(n_size*2)); 
        printf("%d, ", (int) W[w]); 
    }

    for (unsigned int i = 0; i < m_size * n_size; ++i) {
        X[i] = ((float) (rand()%100000 - 50000)) / 10000; 
    }

    for (unsigned int j = 0; j < m_size; ++j) {
        float dot_product = 0.0; 
        for (unsigned int k = 0; k < n_size; ++k) {
            dot_product += X[j*n_size + k] * W[k] + ((float) (rand()%400 - 200)) / 100; 
        }
        double sigmoid_temp = 1 / (1 + exp((double)(-dot_product))); 
        Y[j] = sigmoid_temp >= 0.5 ? 1 : 0; 
    }
    printf("\nSuccessfully generate float input data.\n");
} 

// Create fixed-point input arrays 
static void read_input_fp(const float* X, const float* Y, T* X_fp, T* Y_fp, unsigned int m_size, unsigned int n_size) {
    for (unsigned int j = 0; j < m_size; ++j) {
        Y_fp[j] = Y[j];// * (1 << SHIFT_AMOUNT); 
        for (unsigned int k = 0; k < n_size; ++k) {
            X_fp[j*n_size + k] = X[j*n_size + k] * (1 << SHIFT_AMOUNT); 
        }
    }
    printf("Successfully quantize input data.\n");
}

static int read_input_SUSY(T* X, T* Y, T* W, unsigned int m_size, unsigned int n_size) {
    printf("Reading training dataset from file...\n"); 

    FILE* fp;
    char row[MAXCHAR];
    char* token;
    unsigned int m = 0, n = 0; 

    fp = fopen("/home/upmem0013/gyuxin/SUSY.csv", "r"); // add file path of SUSY.csv 
    if (fp == NULL) {
        perror("Can't open file!");
        return(-1);
    } 

    while (fgets(row, MAXCHAR, fp)) {//(m < m_size) {
        //fgets(row, MAXCHAR, fp); 
        token = strtok(row, ",");
        Y[m] = atof(token); 

        n = 0; 
        token = strtok(NULL, ",");
        while (token != NULL) {
            X[m*n_size + n] = atof(token); 
            token = strtok(NULL, ","); 
            n++; 
        } 
        m++; 
    }
    fclose(fp); 
    printf("\nSuccessfully generate input data. m = %d\n", m); 
    if (m != m_size) {
        printf("Error: invalid input m_size!\n");
        return -1; 
    }
    return 0; 
}

#ifdef FLOAT // float 
// Read training dataset from Skin_NonSkin.txt 
static int read_input_Skin(T* X, T* Y, T* W, unsigned int m_size, unsigned int n_size) {
    printf("Reading training dataset from Skin_NonSkin.csv...\n");

    FILE* fp;
    char row[MAXCHAR];
    char* token;
    unsigned int m = 0, n = 0;

    fp = fopen("../Skin_NonSkin.csv", "r");  // add file path 
    if (fp == NULL) {
        perror("Can't open file!");
        return(-1);
    }

    while (fgets(row, MAXCHAR, fp)) {
        token = strtok(row, ",");
        n = 0;
        while (n < n_size) {//(token != NULL) {
            X[m*n_size + n] = atof(token); 
            token = strtok(NULL, ",");
            n++;
        } 
        char temp = atoi(token);
        if (temp == 1)
            Y[m] = 1.0; 
        else
            Y[m] = 0.0; 
        m++;
    }
    fclose(fp);
    printf("\nSuccessfully generate input data. m = %d\n", m);
    if (m != m_size) {
        printf("Error: invalid input m_size!\n");
        return -1;
    }
}

// Train weight coefficients in the host 
static void GD_host(T* X, T* Y, T* W, uint32_t m_size, uint32_t n_size, uint32_t iter_time, float lr) {
    printf("-----Start traing at host, float-----\n");

    // init wirght with random value
    for (uint32_t n = 0; n < n_size; ++n)
        W[n] = (T) 1; 

    for (uint32_t i = 0; i < iter_time; ++i) {
        
        // calculate gradient 
        T* gradient_tmp = calloc(n_size, sizeof(T)); 

        for (uint32_t j = 0; j < m_size; ++j) {
            T dot_product = 0; 
            for (unsigned int k = 0; k < n_size; ++k) {
                dot_product += X[j*n_size + k] * W[k]; 
            }
            // double sigmoid_temp = sigmoid(dot_product); 
            double sigmoid_temp = 1 / (1 + exp((double)(-dot_product))); 

            for (unsigned int l = 0; l < n_size; ++l) {
                gradient_tmp[l] += X[j*n_size + l] * (sigmoid_temp - Y[j]) / ((int) m_size); 
                // if (j == m_size-1) 
                //     printf("iter: %d, dot_product: %f, sigmoid: %f, Y: %f, inidival gradient: %f\n", \
                //         i, dot_product, sigmoid_temp, Y[j], gradient_tmp[l]); 
            }
            // if(j < 4){
            //     printf("dot_product at host: %f, sigmoid at host: %f\n", dot_product, sigmoid_temp);
            //     printf("X at host: "); 
            //     for (uint32_t each_attribute = 0; each_attribute < n_size; each_attribute++) {
            //         printf("%f, ", X[j*n_size + each_attribute]); 
            //     }
            //     printf("\n"); 
            // }
        } // gradient done 
        
        // update weight
        for (uint32_t m = 0; m < n_size; ++m) {
            W[m] = W[m] - (gradient_tmp[m] * lr); 
        }

        // printf("i: %d, g: %f, g*lr: %.4f, w: %.4f\n", i, gradient_tmp[0], \
        //     ((float) gradient_tmp[0] * lr), W[0]); 

        free(gradient_tmp); 

    } // end iteration
    
}

#else // int 
// Read training dataset from Skin_NonSkin.txt 
static int read_input_Skin(T* X, T* Y, T* W, unsigned int m_size, unsigned int n_size) {
    printf("Reading training dataset from Skin_NonSkin.csv...\n");

    FILE* fp;
    char row[MAXCHAR];
    char* token;
    unsigned int m = 0, n = 0;

    fp = fopen("../Skin_NonSkin.csv", "r");  // add file path 
    if (fp == NULL) {
        perror("Can't open file!");
        return(-1);
    }

    while (fgets(row, MAXCHAR, fp)) {
        token = strtok(row, ",");
        n = 0;
        while (n < n_size) {//(token != NULL) {
            X[m*n_size + n] = atoi(token); 
            token = strtok(NULL, ",");
            n++;
        } 
        char temp = atoi(token);
        if (temp == 1)
            Y[m] = 1; 
        else
            Y[m] = 0; 
        m++;
    }
    fclose(fp);
    printf("\nSuccessfully generate input data. m = %d\n", m);
    if (m != m_size) {
        printf("Error: invalid input m_size!\n");
        return -1;
    }
}

// Train weight coefficients in the host
static void GD_host_fp(T* X, T* Y, T* W, T** y_expected,T** gd_expected, uint32_t m_size, uint32_t n_size, uint32_t iter_time, float lr) {
    printf("-----Start traing at host, fixed-point arithmetic-----\n");

    // init weight with random value
    for (uint32_t n = 0; n < n_size; ++n){
        // W[n] = (T) 1; 
        W[n] = (T) (1 << SHIFT_AMOUNT); 
    }

    for (uint32_t i = 0; i < iter_time; ++i) {
        // calculate gradient 
        T* gradient_tmp = calloc(n_size, sizeof(T)); 

        for (uint32_t j = 0; j < m_size; ++j) {
            T dot_product = 0; 
            for (unsigned int k = 0; k < n_size; ++k) {
                dot_product += X[j*n_size + k] * W[k]; 
            }
            y_expected[i][j] = dot_product;
            T sigmoid_temp = (int32_t) round((1<<SHIFT_AMOUNT)/(1.0 + exp(
                (double) -(dot_product>>SHIFT_AMOUNT)/(1<<SHIFT_AMOUNT)))); 

            for (unsigned int l = 0; l < n_size; ++l) {
                // avoid overflow
                gradient_tmp[l] += X[j*n_size + l] * (sigmoid_temp - \
                    (Y[j]<<SHIFT_AMOUNT)) >> (OVERFLOW_SHIFT+SHIFT_AMOUNT); 
            }
            #if PRINT
            if(j < 4){
                printf("dot_product at host %d, s at host: %d\n", dot_product, sigmoid_temp); 
                printf("X at host: "); 
                for (uint32_t each_attribute = 0; each_attribute < n_size; each_attribute++) {
                    printf("%d, ", X[j*n_size + each_attribute]); 
                }
                printf("\n"); 
            } 
            #endif
        } // gradient done 
        
        // update weight
        for (uint32_t m = 0; m < n_size; ++m) {
            W[m] = W[m] - (gradient_tmp[m] * lr) / (m_size>>OVERFLOW_SHIFT); 
            gd_expected[i][m] = gradient_tmp[m];
        }
        #if PRINT
        printf("i: %d, g: %d, g*lr/m_size: %.4f, w: %d\n", i, gradient_tmp[0], \
            (float)gradient_tmp[0] * lr / (m_size>>OVERFLOW_SHIFT), W[0]); 
        #endif
        free(gradient_tmp); 
    } // end iteration
}
#endif 

static void init_argument_tasklet(uint32_t tasklet_id, uint32_t nr_rows, uint32_t* rows_per_tasklet, uint32_t* start_row){
    unsigned int element_per_cacheY = 8 >> DIV; 
    unsigned int chunks = nr_rows / (NR_TASKLETS * element_per_cacheY);
    unsigned int dbl_chunks = chunks * element_per_cacheY;  
    *rows_per_tasklet = dbl_chunks; // rows per tasklet is multiple of element_per_cacheY
    unsigned int rest_rows = nr_rows % (NR_TASKLETS * element_per_cacheY); 

    if ((tasklet_id * element_per_cacheY) < rest_rows)
        *rows_per_tasklet += element_per_cacheY;
    if (rest_rows > 0) {
        if ((tasklet_id * element_per_cacheY) >= rest_rows) {
            if ((rest_rows % element_per_cacheY) != 0)
                *start_row = roundup(rest_rows, element_per_cacheY) + tasklet_id * dbl_chunks; 
            else
                *start_row = rest_rows + tasklet_id * dbl_chunks; 
        } else 
            *start_row = tasklet_id * (dbl_chunks + element_per_cacheY);
    } else {
        *start_row = tasklet_id * (dbl_chunks);
    }

    // printf("tasklet: %d, start_row: %d, row/tasklet: %d\n", tasklet_id, *start_row, *rows_per_tasklet); 
}

void compute_error_rate(const float* X, const float* Y, const float* W, int m_size, int n_size, char* comment){
    uint32_t reduction = 0; 
    uint32_t sum_of_Y = 0;

    for (int m = 0; m < m_size; ++m) {
        float dot_product = 0.0;
        for (int n = 0; n < n_size; ++n) {
            dot_product += (float) X[m*n_size + n] * W[n]; 
        }
        double sigmoid_temp = 1 / (1 + exp((double)(-dot_product))); 
        int32_t predict_temp = sigmoid_temp >= 0.5 ? 1 : 0; 
        if(predict_temp != (int32_t) Y[m]){
            reduction++; 
        }
        sum_of_Y += Y[m]; 
    }
    printf("error rate on %s = %.2f%%, reduction: %d, sum_of_Y: %d\n", comment, \
        ((float) reduction/m_size)*100, reduction, sum_of_Y); 
}

// Main of the Host Application
int main(int argc, char **argv) {

    struct Params p = input_params(argc, argv);

    struct dpu_set_t dpu_set, dpu;
    uint32_t nr_of_dpus;

#if ENERGY
    struct dpu_probe_t probe;
    DPU_ASSERT(dpu_probe_init("energy_probe", &probe));
#endif

    // Allocate DPUs and load binary
    DPU_ASSERT(dpu_alloc(NR_DPUS, NULL, &dpu_set)); 
    DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY, NULL));
    DPU_ASSERT(dpu_get_nr_dpus(dpu_set, &nr_of_dpus));
    printf("Allocated %d DPU(s)\n", nr_of_dpus);

    unsigned int i = 0;

    unsigned int iter_time = p.iter_time;
    float learning_rate = p.learning_rate; 

    unsigned int m_size = p.m_size;
    unsigned int n_size = p.n_size;

    printf("i = %d, lr = %.4f, m = %d, n = %d\n", iter_time, learning_rate, m_size, n_size); 

    // Initialize help data
    dpu_info = (struct dpu_info_t *) malloc(nr_of_dpus * sizeof(struct dpu_info_t));
    dpu_arguments_t *input_args = (dpu_arguments_t *) malloc(nr_of_dpus * sizeof(dpu_arguments_t));
    uint32_t max_rows_per_dpu = 0;
    uint32_t n_size_pad = ((n_size*sizeof(T)) % 8) == 0 ? n_size : roundup(n_size, (8/sizeof(T))); 
    // printf("%d\n", roundup(n_size, 2)); 

    DPU_FOREACH(dpu_set, dpu, i) {
        uint32_t rows_per_dpu;
        uint32_t prev_rows_dpu = 0;
        uint32_t chunks = m_size / nr_of_dpus;
        rows_per_dpu = chunks;
        uint32_t rest_rows = m_size % nr_of_dpus;
        if (i < rest_rows)
            rows_per_dpu++;

        if (rest_rows > 0) {
            if (i >= rest_rows)
                prev_rows_dpu = rest_rows + i * chunks; 
                // prev_rows_dpu = rest_rows * (chunks + 1) + (i - rest_rows) * chunks;
            else
                prev_rows_dpu = i * (chunks + 1);
        } else {
            prev_rows_dpu = i * chunks;
        }

        // Keep max rows for parallel transfers
        uint32_t rows_per_dpu_pad = ((rows_per_dpu*sizeof(T)) % 8) == 0 ? rows_per_dpu : roundup(rows_per_dpu, (8/sizeof(T))); 
        if (rows_per_dpu_pad > max_rows_per_dpu)
            max_rows_per_dpu = rows_per_dpu_pad;

        dpu_info[i].rows_per_dpu = rows_per_dpu;
        dpu_info[i].rows_per_dpu_pad = rows_per_dpu_pad;
        dpu_info[i].prev_rows_dpu = prev_rows_dpu;

        // Copy input arguments to DPU
        input_args[i].n_size = n_size;
        input_args[i].n_size_pad = n_size_pad;
        input_args[i].nr_rows = rows_per_dpu;

        // Init arguments for each tasklet
        for(uint32_t id = 0; id < NR_TASKLETS; ++id) {
            init_argument_tasklet(id, rows_per_dpu, &input_args[i].rows_per_tasklet[id], \
                &input_args[i].start_row[id]); 
            // printf("%d, start row %d, row/tasklet %d\n", input_args[i].start_row[id], \
            //     input_args[i].rows_per_tasklet[id]); 
        }

        // printf("row per dpu: %d\n", rows_per_dpu);
    }
    omp_set_num_threads(8); 
    // Input/output allocation
    X = malloc(max_rows_per_dpu * nr_of_dpus * n_size_pad * sizeof(T)); 
    Y = malloc(max_rows_per_dpu * nr_of_dpus * sizeof(T)); 
    W = malloc(n_size_pad * sizeof(T)); 
    T* X_C = malloc(max_rows_per_dpu * nr_of_dpus * n_size_pad * sizeof(T)); 
    T* X_D = malloc(max_rows_per_dpu * nr_of_dpus * n_size_pad * sizeof(T)); 

    T** y_expected = malloc(iter_time * sizeof(T*)); 
    for(int i=0; i< iter_time; i++){
        y_expected[i] = malloc(max_rows_per_dpu * nr_of_dpus * sizeof(T));
    }
    T* Y_host = malloc(max_rows_per_dpu * nr_of_dpus * sizeof(T)); 
    T* Y_dpu = malloc(max_rows_per_dpu * nr_of_dpus * sizeof(T)); 
    T* Y_total = malloc(max_rows_per_dpu * nr_of_dpus * sizeof(T)); 
    T* Y_temp = malloc(max_rows_per_dpu * nr_of_dpus * sizeof(T)); 


    T* product = malloc(max_rows_per_dpu * nr_of_dpus * sizeof(T)); 
    T* sigmoid = malloc(max_rows_per_dpu * nr_of_dpus * sizeof(T)); 
    uint32_t operation_mode=0;
    uint8_t* b1=calloc(max_rows_per_dpu * nr_of_dpus,sizeof(uint8_t));
    uint8_t* b2=calloc(max_rows_per_dpu * nr_of_dpus,sizeof(uint8_t));

    // init trainging dataset and weight for host 
    T *bufferX = X;
    T *bufferY = Y;
    T *bufferW_host = W; 
    // T* bufferW_fp = malloc(n_size_pad * sizeof(T)); 

    // init training dataset and initial host W
    # ifdef FLOAT 
    read_input_float(bufferX, bufferY, bufferW_host, m_size, n_size);
    # else 
    float* X_float = (float*) malloc(m_size * n_size* sizeof(float)); 
    float* Y_float = (float*) malloc(m_size * sizeof(float)); 
    read_input_float(X_float, Y_float, bufferW_host, m_size, n_size); 
    read_input_fp(X_float, Y_float, bufferX, bufferY, m_size, n_size); 
    #endif
    // read_input_SUSY(bufferX, bufferY, bufferW_host, m_size, n_size); 

    float W_predef[n_size];
    for (uint32_t n = 0; n < n_size; ++n){
        // W_predef[n] = 0; 
        W_predef[n] = (float) n+1;//bufferW_host[n]; 
    }

    // init Weight for DPU 
    T* W_dpu = malloc(n_size_pad * sizeof(T)); 
    T* W_dpu_fp = malloc(n_size_pad * sizeof(T)); 
    for (uint32_t n = 0; n < n_size_pad; ++n) {
        W_dpu[n] = (T) 1.0; 
        W_dpu_fp[n] = (T) (1 << SHIFT_AMOUNT); 
    }

    // temp dpu gradient  
    T* gradient_dpu_tmp = malloc(n_size_pad * nr_of_dpus * sizeof(T)); 
    T** gd_expected = malloc(iter_time * sizeof(T*)); 
    for(int i=0; i< iter_time; i++){
        gd_expected[i] = malloc(n_size * sizeof(T));
    }
    // Timer declaration
    Timer timer;

    T* tags1 = malloc(n_size * sizeof(T));
    T* tags2 = malloc(m_size * sizeof(T));
    srand(0);
    for(unsigned int i=0; i< n_size; i++){ //we consider these tags are precomputed 
       tags1[i] = rand()%25;
    }
    srand(0);
    for(unsigned int i=0; i< m_size; i++){ //we consider these tags are precomputed
       tags2[i] = rand()%25;
    }

    // Train the model on host

    start(&timer, 0, 0);
    #ifdef FLOAT 
    GD_host(bufferX, bufferY, bufferW_host, m_size, n_size, iter_time, learning_rate); 
    // printf("sigmoid: %f, x: %f\n", sigmoid_dpu(0), 1.0); 
    #else 
    GD_host_fp(bufferX, bufferY, bufferW_host,y_expected, gd_expected,m_size, n_size, iter_time, learning_rate); 
    #endif 
    stop(&timer, 0); 

    #ifdef FLOAT 
    // compute_error_rate(bufferX, bufferY, W_predef, m_size, n_size, "host_ideal"); 
    // #else 
    // compute_error_rate(X_float, Y_float, W_predef, m_size, n_size, "host_ideal"); 
    #endif 
    
    //Generation ciphertext
    uint8_t key[] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
	struct AES_ctx ctx;
    uint8_t* counter = malloc(max_rows_per_dpu * nr_of_dpus * n_size_pad * sizeof(uint8_t));
    // #pragma omp parallel for
   	for(uint32_t i=0;i< max_rows_per_dpu * nr_of_dpus * n_size_pad; i++){
		counter[i] = (uint8_t)(0+(i*sizeof(T)));//(uint8_t)(bufferX+(i*sizeof(T)));
    }

    AES_init_ctx(&ctx, key);
    AES_ECB_encrypt(&ctx, counter);
	// #pragma omp parallel for
    for(uint32_t i=0;i<max_rows_per_dpu * nr_of_dpus * n_size_pad; i++){
        X_C[i] = (T)counter[i];
		X_D[i]= bufferX[i] - X_C[i];
	}

    // Transfer input arguments and training dataset to DPU
    printf("Load input data to DPUs\n");
    start(&timer, 1, 0); // CPU-DPU transfer time start
    // Input arguments
    i = 0;
    DPU_FOREACH(dpu_set, dpu, i) {
        // Copy input arguments to DPU
        input_args[i].max_rows = max_rows_per_dpu;

        DPU_ASSERT(dpu_prepare_xfer(dpu, input_args + i));
    }
    DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0, sizeof(dpu_arguments_t), \
        DPU_XFER_DEFAULT)); 

    // Copy X and y 
    // printf("arguments sent\n");
    i = 0;
    DPU_FOREACH(dpu_set, dpu, i) {
        DPU_ASSERT(dpu_prepare_xfer(dpu, X_D + dpu_info[i].prev_rows_dpu * n_size));
    }
    DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, \
        max_rows_per_dpu * n_size_pad * sizeof(T), DPU_XFER_DEFAULT)); 
    
    i = 0;
    DPU_FOREACH(dpu_set, dpu, i) {
        DPU_ASSERT(dpu_prepare_xfer(dpu, bufferY + dpu_info[i].prev_rows_dpu)); 
    }
    DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, \
        max_rows_per_dpu * n_size_pad * sizeof(T), max_rows_per_dpu * sizeof(T), DPU_XFER_DEFAULT)); 

    // printf("x,y sent\n");

    stop(&timer, 1); // CPU-DPU transfer time stop

    // Iteration at DPU
    printf("Run program on DPU(s)...\n"); 
    for(uint32_t rep = 0; rep < iter_time; ++rep) {
        start(&timer, 2, rep); // CPU-DPU transfer time start
        i = 0;
        operation_mode=0;
        DPU_FOREACH(dpu_set, dpu, i) {
            // Copy input arguments to DPU
            DPU_ASSERT(dpu_prepare_xfer(dpu, &operation_mode));
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "op_mode", 0, \
            sizeof(uint32_t), DPU_XFER_DEFAULT));

        // printf("mode  sent\n");
        // Copy W 
        
        i = 0; 
        DPU_FOREACH(dpu_set, dpu, i) {
            #ifdef FLOAT
            DPU_ASSERT(dpu_prepare_xfer(dpu, W_dpu)); 
            #else
            DPU_ASSERT(dpu_prepare_xfer(dpu, W_dpu_fp)); 
            #endif 
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, \
            max_rows_per_dpu * n_size_pad * sizeof(T) + max_rows_per_dpu * sizeof(T), \
            n_size_pad * sizeof(T), DPU_XFER_DEFAULT)); 
        stop(&timer, 2); // CPU-DPU transfer time stop 
    // printf("weight\n");
    
    //cpu kernel computation

        AES_init_ctx(&ctx, key);
        start(&timer, 6, rep);
        // int partition = m_size/PART;
        uint32_t partition = m_size/PART;
        #pragma omp parallel
        {
            #pragma omp for schedule(static) 
            for(uint32_t s=0; s < (PART); s++){ 
                uint8_t* counter1 = (uint8_t*) aligned_alloc(64, ((m_size * n_size)/PART) * sizeof(uint8_t));
                // uint8_t* counter1 = (uint8_t*) malloc(((m_size * n_size)/PART) * sizeof(uint8_t));
                for(uint32_t i=0; i < partition; i++){ 
                    int offset = (partition* s) + i;
                    for (unsigned int k = 0; k < n_size; k++) { //Just getting some random counters
                        counter1[i*n_size+k] = (uint8_t)(offset+ k * sizeof(T));
                    }
                }
                AES_ECB_encrypt(&ctx, counter1);
                for(uint32_t i=0;i < partition; i++){ 
                    // int offset = (partition * s) + i;
                    int temp = 0; 
                    // #pragma omp simd reduction(+:temp)
                    for (unsigned int k = 0; k < n_size; k++) {
                        temp += counter1[i*n_size+k] * W_dpu_fp[k];
                    }
                    Y_host[((partition * s) + i)]=temp;
                }
                free(counter1);
            }
        }
        T tagIterCurrent = 0; 
        for( unsigned int t = 0; t < n_size; t++){
            tagIterCurrent += tags1[t] * W_dpu_fp[t];
        }
        stop(&timer, 6);
        // Calculate elapsed time in milliseconds
        
        // Run DPU kernel
        start(&timer, 3, rep); 
        #if ENERGY
        DPU_ASSERT(dpu_probe_start(&probe)); 
        #endif
        
        // Launch kernel 
        DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS)); 

        stop(&timer, 3);
        #if ENERGY
        DPU_ASSERT(dpu_probe_stop(&probe));
        #endif

        #if PRINT 
        if (rep%1 == 0) {
            unsigned int each_dpu = 0;
            printf("Display DPU Logs\n");
            DPU_FOREACH (dpu_set, dpu) {
                if (each_dpu == 0){
                    printf("DPU#%d:\n", each_dpu);
                    DPU_ASSERT(dpulog_read_for_dpu(dpu.dpu, stdout)); 
                }
                each_dpu++;
            }
        }
        #endif 

        
        start(&timer, 4, rep); // DPU-CPU time 
        DPU_FOREACH(dpu_set, dpu, i) {
            // printf("prev:%d\n",dpu_info[i].prev_rows_dpu);
            DPU_ASSERT(dpu_prepare_xfer(dpu, product + dpu_info[i].prev_rows_dpu)); 
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, "dot_product_t", 0, max_rows_per_dpu * sizeof(T), \
            DPU_XFER_DEFAULT));
        stop(&timer, 4); // DPU-CPU time 

        start(&timer, 7, rep);
        //merge

        #pragma omp parallel
        {
            #pragma omp for schedule(static) 
            for(uint32_t i=0;i< m_size; i++){
                Y_total[i] = product[i] + Y_host[i];
            }
        }

        stop(&timer, 7);

        int s1=2;
        T verif=0;
        int flag1 =0;

        start(&timer, 9, rep);
        int powers = 1;
        for (uint32_t i = 0; i < m_size; i++){
            if (powers == 64) powers=1;
            else powers *= s1;
            verif+= (Y_total[i]) * powers;
        }
        if(verif == tagIterCurrent){// Since we are using random numbers instead of precomputed tags this is not = True 
            flag1 = 1;// printf("Verified\n"); 
        }
        stop(&timer, 9);

        start(&timer, 10, rep);
        // #pragma omp paralell for
        #pragma omp parallel
        {
            #pragma omp for schedule(static) 
            for(int j=0; j < m_size; j++){
                if(0.5+Y_total[j] >= 0) b1[j]=0;
                else b1[j]=1;
                if(Y_total[j]-0.5 >= 0) b2[j]=0;
                else b2[j]=1;
                sigmoid[j]= (~b2[j]) + (~b1[j] & b2[j]) * Y_total[j];
            }
        }

        stop(&timer, 10); 
                // end
        //send back results
        start(&timer, 2, rep);
        i = 0;
        operation_mode=1;
        DPU_FOREACH(dpu_set, dpu, i) {
        // Copy input arguments to DPU
            DPU_ASSERT(dpu_prepare_xfer(dpu, &operation_mode));
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "op_mode", 0, \
        sizeof(uint32_t), DPU_XFER_DEFAULT));

        i = 0;
        DPU_FOREACH(dpu_set, dpu, i) {
        // Copy input arguments to DPU
            DPU_ASSERT(dpu_prepare_xfer(dpu, b1 + dpu_info[i].prev_rows_dpu));
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "b1", 0, \
        max_rows_per_dpu  * sizeof(uint8_t), DPU_XFER_DEFAULT));
        
        i=0;
        DPU_FOREACH(dpu_set, dpu, i) {
        // Copy input arguments to DPU
            DPU_ASSERT(dpu_prepare_xfer(dpu, b2 +  dpu_info[i].prev_rows_dpu));
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "b2", 0, \
        max_rows_per_dpu * sizeof(uint8_t), DPU_XFER_DEFAULT));

        stop(&timer, 2); 

        // Run DPU kernel
        start(&timer, 3, rep); 
        #if ENERGY
        DPU_ASSERT(dpu_probe_start(&probe)); 
        #endif
        
        // Launch kernel 
        DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS)); 

        stop(&timer, 3);

        for(int j = 0; j < m_size; j++) {
            Y_temp[j] = (Y[j] << SHIFT_AMOUNT);
        }

        #if ENERGY
        DPU_ASSERT(dpu_probe_stop(&probe));
        #endif
        T* gradient_cpu = calloc(n_size, sizeof(T));
        AES_init_ctx(&ctx, key);

        partition = m_size/PART2;
        int shift = (OVERFLOW_SHIFT+SHIFT_AMOUNT);
        start(&timer, 8, rep);
            
        // #pragma omp parallel for schedule(static, 256)
        
        for(int j = 0; j < m_size; j++) {
            Y_temp[j] = sigmoid[j] - Y_temp[j];
        }

        int tagIterCurrent1 = 0;
        for (int s = 0; s < PART2; s++) {
            uint8_t* counter2 = malloc((m_size * n_size)/PART2 * sizeof(uint8_t));
            for(uint32_t i=0; i < partition; i++){ 
                int offset = (partition * s) + i;
                for (unsigned int k = 0; k < n_size; k++) {// just getting random counter (seed for this can be precomputed)
                    counter2[i*n_size+k] = (uint8_t)((offset)+k * sizeof(T));//(uint8_t)(bufferX + (k * sizeof(T)));//[s*(max_rows_per_dpu * nr_of_dpus * n_size_pad/PART)+(i*(n_size_pad)+k)]);
                }
            }
            AES_ECB_encrypt(&ctx, counter2);
            for (uint32_t i = 0; i < partition; i++) {
                int offset = s * partition + i;
                for (unsigned int k = 0; k < n_size; k++) {
                    gradient_cpu[k]+= counter2[(i * n_size) + k] * Y_temp[offset] >> shift;
                }
                // tagIterCurrent1 += tags2[offset] * Y_temp[offset]  >> (OVERFLOW_SHIFT+SHIFT_AMOUNT);
            }
            free(counter2);
        }
        for(int j = 0; j < m_size; j++) {
            tagIterCurrent1 += (tags2[j] * Y_temp[j]) >> shift;
        }

        stop(&timer, 8);

    
        // Retrive result
        start(&timer, 4, rep); 
        DPU_FOREACH(dpu_set, dpu, i) {
            DPU_ASSERT(dpu_prepare_xfer(dpu, gradient_dpu_tmp + i * n_size_pad)); 
        }
        DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, "DPU_RESULTS", 0, n_size_pad * sizeof(T), \
            DPU_XFER_DEFAULT)); 
        stop(&timer, 4); 
        // printf("dpu results sent\n");
        
        start(&timer, 5, rep); // CPU reduction 
        // Compute gradient
        T* gradient_dpu = calloc(n_size, sizeof(T)); 
        T* total_gradient = calloc(n_size, sizeof(T)); 
        i = 0; 
        DPU_FOREACH(dpu_set, dpu, i) {
            for (uint32_t x = 0; x < n_size; ++x) {
                #ifdef FLOAT 
                gradient_dpu[x] += gradient_dpu_tmp[i*n_size_pad + x] / (int) m_size; 
                #else 
                gradient_dpu[x] += gradient_dpu_tmp[i*n_size_pad + x] >> OFFSET; 
                #endif 
            }
        } 
        // start(&timer, 10, rep);
        for (uint32_t x = 0; x < n_size; ++x) {
            total_gradient[x]= gradient_dpu[x] + gradient_cpu[x];
            
        }
        // stop(&timer,10);
        int s2=10;
        T verifi=0;
        start(&timer, 11, rep);
        int powers1 = 1;
        for (unsigned int i = 0; i < n_size; i++){
            if(powers1 == 64) powers1 =1;
            else powers1 *= s1;
            verifi += (total_gradient[i]) * powers1;
            }
        if( tagIterCurrent1 == verifi){
            printf("verified\n");
        }
        stop(&timer, 11);
        // Update weight 
        for (uint32_t m = 0; m < n_size; ++m) { 
            #ifdef FLOAT 
            // float 
            W_dpu[m] = W_dpu[m] - (gradient_dpu[m]*learning_rate); 
            #else 
            // int 
            W_dpu_fp[m] = W_dpu_fp[m] - (gradient_dpu[m]*learning_rate) / (m_size >> OVERFLOW_SHIFT); 
            // printf("iter: %d, gradient_dpu: %d, W_dpu_fp: %d\n", rep, gradient_dpu[m], W_dpu_fp[m]); 
            #endif 
        }
        

        free(gradient_dpu); 
        stop(&timer, 5); // CPU reduction 

        if (rep % 100 == 0)
            printf("DPU iter %d...\n", rep); 
    } // iter end 

    // Print trained weight at host 
    #ifdef INT32 
    float* W_host_float = (float*) malloc(n_size*sizeof(float));
    float* W_dpu_float  = (float*) malloc(n_size*sizeof(float));
    #endif
    printf("Trained weight at host: ");
    for (uint32_t x = 0; x < n_size; ++x) {
        #ifdef FLOAT
        printf("%.3f, ", (float) bufferW_host[x]); 
        #else
        W_host_float[x] = (float) bufferW_host[x] / (1<<SHIFT_AMOUNT); 
        printf("%.3f, ", W_host_float[x]); 
        #endif
    }
    printf("\n"); 

    // Print DPU trained result 
    printf("Trained weight at DPU: ");
    for (uint32_t m = 0; m < n_size; ++m) {
        #ifdef FLOAT
        printf("%.3f, ", (float) W_dpu[m]); 
        #else
        W_dpu_float[m] = (float) W_dpu_fp[m] / (SHIFT_MASK + 1); 
        printf("%.3f, ", W_dpu_float[m]); 
        #endif
    }
    printf("\n"); 

    // Print timing results
    printf("CPU (ms)");
    print(&timer, 0, 1);
    printf("\n"); 

    // printf("init C-D ");
    // print(&timer, 1, 1);
    // printf("syn C-D ");
    // print(&timer, 2, 1); 
    // printf("DPU kernel ");
    // print(&timer, 3, 1);
    // printf("D-C ");
    // print(&timer, 4, 1);
    // printf("CPU Part 1 ");
    // print(&timer, 6, 1);
    // printf("merge 1 ");
    // print(&timer, 7, 1);
    // printf("verif 1 ");
    // print(&timer, 9, 1);
    // printf("CPU sigmoid ");
    // print(&timer, 10, 1);
    // printf("CPU Part 2 ");
    // print(&timer, 8, 1);
    // printf("CPU reduction (merge 2) ");
    // print(&timer, 5, 1);
    // printf("verif 2 ");
    // print(&timer, 11, 1);

    float cpuside = (timer.time[6]+timer.time[8]) / (1000);
    float dpuside = (timer.time[1]+timer.time[2]+timer.time[3]+timer.time[4]) / (1000);
    float execution_time = fmax(cpuside,dpuside) + (timer.time[7]+timer.time[9] +timer.time[5]+ timer.time[11]+timer.time[10])/ (1000);
    float actual_time = dpuside + (timer.time[7]+timer.time[9] +timer.time[5]+ timer.time[11]+timer.time[10])/ (1000);
    printf("\n\nExecution time with CPU overhead : %f ms", execution_time ); //UPMEM servers has some extra overhead on CPU side
    printf("\n\nExecution time without CPU overhead: %f ms\n\n", actual_time );


// #if ENERGY
//     double energy;
//     DPU_ASSERT(dpu_probe_get(&probe, DPU_ENERGY, DPU_AVERAGE, &energy));
//     printf("DPU Energy (J): %f\t", energy);
// #endif   

    // Check output
    bool status = true; 
    for (uint32_t each_attr = 0; each_attr < n_size; ++each_attr) {
        #ifdef FLOAT 
        if ((bufferW_host[each_attr] - W_dpu[each_attr] > 0.01) || 
            (bufferW_host[each_attr] - W_dpu[each_attr] < -0.01)) 
        {
            status = false; 
            # if PRINT
            printf("host: %.2f, dpu: %.2f\n", (float) bufferW_host[each_attr], (float) W_dpu[each_attr]); 
            #endif
        }
        #else
        if ((W_host_float[each_attr] - W_dpu_float[each_attr] > 0.01) || 
            (W_host_float[each_attr] - W_dpu_float[each_attr] < -0.01)) 
        {
            status = false; 
            // # if PRINT
            // printf("host: %.2f, dpu: %.2f\n", (float) bufferW_host[each_attr], (float) W_dpu[each_attr]); 
            // #endif
        }
        #endif
    }

    if (status) {
        printf("[" ANSI_COLOR_GREEN "OK" ANSI_COLOR_RESET "] Outputs are equal\n");
    } else {
        printf("[" ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET "] Outputs differ!\n");
    }

    # ifdef FLOAT
    compute_error_rate(bufferX, bufferY, bufferW_host, m_size, n_size, "host"); 
    compute_error_rate(bufferX, bufferY, W_dpu, m_size, n_size, "DPUs"); 
    # else
    compute_error_rate(X_float, Y_float, W_host_float, m_size, n_size, "host"); 
    compute_error_rate(X_float, Y_float, W_dpu_float, m_size, n_size, "DPUs"); 
    free(X_float); 
    free(Y_float); 
    # endif

    #ifdef INT32 
    free(W_host_float);
    free(W_dpu_float); 
    #endif

    // Deallocation
    free(input_args); 
    free(X);
    free(Y);
    free(W);
    free(W_dpu); 
    free(W_dpu_fp); 
    free(gradient_dpu_tmp); 
    DPU_ASSERT(dpu_free(dpu_set));
    
    return status ? 0 : -1;
}
