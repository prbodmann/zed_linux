#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include <string.h>

#define MAX_ERR_ITER_LOG 500

#define BLOCK_SIZE 16
#define BLOCK_SIZE_C BLOCK_SIZE
#define BLOCK_SIZE_R BLOCK_SIZE

#define STR_SIZE	256

/* maximum power density possible (say 300W for a 10mm x 10mm chip)	*/
#define MAX_PD	(3.0e6)
/* required precision in degrees	*/
#define PRECISION	0.001
#define SPEC_HEAT_SI 1.75e6
#define K_SI 100
/* capacitance fitting factor	*/
#define FACTOR_CHIP	0.5

//#define NUM_THREAD 4

/* Define the precision to float or double depending on compiling flags */
#if FP == 32
typedef float FLOAT;
#define FP_STR "Float"
#endif
#if FP == 64
typedef double FLOAT;
#define FP_STR "Double"
#endif
/* Default to double if no compiling flags are used */
#ifndef FP_STR
typedef double FLOAT;
#define FP_STR "Double"
#endif


/* chip parameters	*/
const FLOAT t_chip = 0.0005;
const FLOAT chip_height = 0.016;
const FLOAT chip_width = 0.016;

/* ambient temperature, assuming no package at all	*/
const FLOAT amb_temp = 80.0;


/* Single iteration of the transient solver in the grid model.
 * advances the solution of the discretized difference equations
 * by one time step
 */
void single_iteration(FLOAT *result, FLOAT *temp, FLOAT *power, int row, int col,
                      FLOAT Cap_1, FLOAT Rx_1, FLOAT Ry_1, FLOAT Rz_1,
                      FLOAT step)
{
    FLOAT delta;
    int r, c;
    int chunk;
    int num_chunk = row*col / (BLOCK_SIZE_R * BLOCK_SIZE_C);
    int chunks_in_row = col/BLOCK_SIZE_C;
    int chunks_in_col = row/BLOCK_SIZE_R;

    for ( chunk = 0; chunk < num_chunk; ++chunk )
    {
        int r_start = BLOCK_SIZE_R*(chunk/chunks_in_col);
        int c_start = BLOCK_SIZE_C*(chunk%chunks_in_row);
        int r_end = r_start + BLOCK_SIZE_R > row ? row : r_start + BLOCK_SIZE_R;
        int c_end = c_start + BLOCK_SIZE_C > col ? col : c_start + BLOCK_SIZE_C;

        if ( r_start == 0 || c_start == 0 || r_end == row || c_end == col )
        {
            for ( r = r_start; r < r_start + BLOCK_SIZE_R; ++r ) {
                for ( c = c_start; c < c_start + BLOCK_SIZE_C; ++c ) {
                    /* Corner 1 */
                    if ( (r == 0) && (c == 0) ) {
                        delta = (Cap_1) * (power[0] +
                                           (temp[1] - temp[0]) * Rx_1 +
                                           (temp[col] - temp[0]) * Ry_1 +
                                           (amb_temp - temp[0]) * Rz_1);
                    }	/* Corner 2 */
                    else if ((r == 0) && (c == col-1)) {
                        delta = (Cap_1) * (power[c] +
                                           (temp[c-1] - temp[c]) * Rx_1 +
                                           (temp[c+col] - temp[c]) * Ry_1 +
                                           (   amb_temp - temp[c]) * Rz_1);
                    }	/* Corner 3 */
                    else if ((r == row-1) && (c == col-1)) {
                        delta = (Cap_1) * (power[r*col+c] +
                                           (temp[r*col+c-1] - temp[r*col+c]) * Rx_1 +
                                           (temp[(r-1)*col+c] - temp[r*col+c]) * Ry_1 +
                                           (   amb_temp - temp[r*col+c]) * Rz_1);
                    }	/* Corner 4	*/
                    else if ((r == row-1) && (c == 0)) {
                        delta = (Cap_1) * (power[r*col] +
                                           (temp[r*col+1] - temp[r*col]) * Rx_1 +
                                           (temp[(r-1)*col] - temp[r*col]) * Ry_1 +
                                           (amb_temp - temp[r*col]) * Rz_1);
                    }	/* Edge 1 */
                    else if (r == 0) {
                        delta = (Cap_1) * (power[c] +
                                           (temp[c+1] + temp[c-1] - 2.0*temp[c]) * Rx_1 +
                                           (temp[col+c] - temp[c]) * Ry_1 +
                                           (amb_temp - temp[c]) * Rz_1);
                    }	/* Edge 2 */
                    else if (c == col-1) {
                        delta = (Cap_1) * (power[r*col+c] +
                                           (temp[(r+1)*col+c] + temp[(r-1)*col+c] - 2.0*temp[r*col+c]) * Ry_1 +
                                           (temp[r*col+c-1] - temp[r*col+c]) * Rx_1 +
                                           (amb_temp - temp[r*col+c]) * Rz_1);
                    }	/* Edge 3 */
                    else if (r == row-1) {
                        delta = (Cap_1) * (power[r*col+c] +
                                           (temp[r*col+c+1] + temp[r*col+c-1] - 2.0*temp[r*col+c]) * Rx_1 +
                                           (temp[(r-1)*col+c] - temp[r*col+c]) * Ry_1 +
                                           (amb_temp - temp[r*col+c]) * Rz_1);
                    }	/* Edge 4 */
                    else if (c == 0) {
                        delta = (Cap_1) * (power[r*col] +
                                           (temp[(r+1)*col] + temp[(r-1)*col] - 2.0*temp[r*col]) * Ry_1 +
                                           (temp[r*col+1] - temp[r*col]) * Rx_1 +
                                           (amb_temp - temp[r*col]) * Rz_1);
                    }
                    result[r*col+c] =temp[r*col+c]+ delta;
                }
            }
            continue;
        }

        for ( r = r_start; r < r_start + BLOCK_SIZE_R; ++r ) {
         
            for ( c = c_start; c < c_start + BLOCK_SIZE_C; ++c ) {
                /* Update Temperatures */
                result[r*col+c] =temp[r*col+c]+
                                 ( Cap_1 * (power[r*col+c] +
                                            (temp[(r+1)*col+c] + temp[(r-1)*col+c] - 2.f*temp[r*col+c]) * Ry_1 +
                                            (temp[r*col+c+1] + temp[r*col+c-1] - 2.f*temp[r*col+c]) * Rx_1 +
                                            (amb_temp - temp[r*col+c]) * Rz_1));
            }
        }
    }
}

/* Transient solver driver routine: simply converts the heat
 * transfer differential equations to difference equations
 * and solves the difference equations by iterating
 */
void compute_tran_temp(FLOAT *result, int num_iterations, FLOAT *temp, FLOAT *power, int row, int col)
{

    FLOAT grid_height = chip_height / row;
    FLOAT grid_width = chip_width / col;

    FLOAT Cap = FACTOR_CHIP * SPEC_HEAT_SI * t_chip * grid_width * grid_height;
    FLOAT Rx = grid_width / (2.0 * K_SI * t_chip * grid_height);
    FLOAT Ry = grid_height / (2.0 * K_SI * t_chip * grid_width);
    FLOAT Rz = t_chip / (K_SI * grid_height * grid_width);

    FLOAT max_slope = MAX_PD / (FACTOR_CHIP * t_chip * SPEC_HEAT_SI);
    FLOAT step = PRECISION / max_slope / 1000.0;

    FLOAT Rx_1=1.f/Rx;
    FLOAT Ry_1=1.f/Ry;
    FLOAT Rz_1=1.f/Rz;
    FLOAT Cap_1 = step/Cap;

    FLOAT* r = result;
    FLOAT* t = temp;
    int i = 0;
    for (i = 0; i < num_iterations ; i++)
    {
        single_iteration(r, t, power, row, col, Cap_1, Rx_1, Ry_1, Rz_1, step);
        FLOAT* tmp = t;
        t = r;
        r = tmp;
    }
}

void fatal(char *s)
{
    fprintf(stderr, "error: %s\n", s);
    exit(-1);
}


void read_gold(FLOAT *vect, int grid_rows, int grid_cols, char *file)
{
    int i, index;
    FILE *fp;
    char str[STR_SIZE];
    FLOAT val;

    fp = fopen (file, "rb");
    if (!fp)
        fatal ("file could not be opened for reading");

    fread(&vect[0], grid_rows*grid_cols, sizeof(FLOAT), fp);

    fclose(fp);
}
void read_input(FLOAT *vect, int grid_rows, int grid_cols, char *file)
{
    int i, index;
    FILE *fp;
    char str[STR_SIZE];
    FLOAT val;

    fp = fopen (file, "r");
    if (!fp)
        fatal ("file could not be opened for reading");

    for (i=0; i < grid_rows * grid_cols; i++) {
        if (fgets(str, STR_SIZE, fp) == NULL) {
            fatal("fgets error");
        }
        if (feof(fp))
            fatal("not enough lines in file");
        if ((sscanf(str, "%f", &val) != 1) )
            fatal("invalid file format");
        vect[i] = val;
    }

    fclose(fp);
}

void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: %s <ip addr> <port> <grid_rows> <grid_cols> <sim time> <temp_file> <power_file> <output_file>\n", argv[0]);
    fprintf(stderr, "\t<ip addr> - ip\n");
    fprintf(stderr, "\t<port> - port\n");
    fprintf(stderr, "\t<grid_rows>  - number of rows in the grid (positive integer)\n");
    fprintf(stderr, "\t<grid_rows>  - number of rows in the grid (positive integer)\n");
    fprintf(stderr, "\t<grid_cols>  - number of columns in the grid (positive integer)\n");
    fprintf(stderr, "\t<temp_file>  - name of the file containing the initial temperature values of each cell\n");
    fprintf(stderr, "\t<power_file> - name of the file containing the dissipated power values of each cell\n");
    fprintf(stderr, "\t<output_file> - name of the output file\n");
    exit(1);
}

int s;
struct sockaddr_in server;
unsigned int buffer[10];
void setup_socket(char* ip_addr, int port){
    s=socket(PF_INET, SOCK_DGRAM, 0);
    //memset(&server, 0, sizeof(struct sockaddr_in));
    //printf("port: %d",port);
    //printf("ip: %s", ip_addr);
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip_addr);

}

void send_message(size_t size){
    //printf("message sent\n");
    sendto(s,buffer,4*size,0,(struct sockaddr *)&server,sizeof(server));
}



int main(int argc, char **argv)
{

    int grid_rows, grid_cols, sim_time, i;
    FLOAT *temp, *power, *result, *final_result, *gold;
    char *tfile, *pfile, *golden_file;

    /* check validity of inputs	*/
    if (argc != 9)
        usage(argc, argv);
    if ((grid_rows = atoi(argv[3])) <= 0 ||
            (grid_cols = atoi(argv[4])) <= 0 ||
            (sim_time = atoi(argv[5])) <= 0             
       )
    usage(argc, argv);
    unsigned int port = atoi(argv[2]);
    setup_socket(argv[1],port);
    /* allocate memory for the temperature and power arrays	*/


    /* read initial temperatures and input power	*/
    tfile = argv[6];
    pfile = argv[7];
    golden_file = argv[8];
    gold = (FLOAT *) calloc (grid_rows * grid_cols, sizeof(FLOAT));
    read_gold(gold, grid_rows, grid_cols, golden_file);
    while(1)    	
    {
	    temp = (FLOAT *) calloc (grid_rows * grid_cols, sizeof(FLOAT));
	    power = (FLOAT *) calloc (grid_rows * grid_cols, sizeof(FLOAT));
	    result = (FLOAT *) calloc (grid_rows * grid_cols, sizeof(FLOAT));
	    
	    read_input(temp, grid_rows, grid_cols, tfile);
        read_input(power, grid_rows, grid_cols, pfile);    
             
        compute_tran_temp(result,sim_time, temp, power, grid_rows, grid_cols);
        int flag=0;

        for (i=0; i < grid_rows; i++) {
            int j;
            for (j=0; j < grid_cols; j++) {
                if (result[i*grid_cols+j] != gold[i*grid_cols+j]) {
                //if(final_result[i*grid_cols+j] != gold[i*grid_cols+j] ) {
                    if(flag==0){
                            buffer[0] = 0xDD000000;
                            flag=1;
                        }else{
                            buffer[0] = 0xCC000000;
                        }                        
                        buffer[1] = *((unsigned int*)&i);
                        buffer[2] = *((unsigned int*)&j);
                        unsigned long long aux=*((unsigned long long*)&result[i*grid_cols+j]);
                        buffer[3] = (unsigned int)((aux & 0xFFFFFFFF00000000LL) >> 32);                      
                        buffer[3] = (unsigned int)(aux & 0xFFFFFFFFLL);
                        send_message(5);
                }
            }
        }
        if(flag==0){
            buffer[0] = 0xAA000000;
              send_message(1); 
        }
    free(temp);
    free(power);
    free(result);
    }
    /* cleanup	*/

    return 0;
}

