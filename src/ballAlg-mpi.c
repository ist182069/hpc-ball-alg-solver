#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <math.h>
#include <string.h>
#include <mpi.h>
#include "gen_points.h"
#include "point_operations.h"
#include "ball_tree.h"
#include "macros.h"

//#define DEBUG
//remove the above comment to enable debug messages

int n_dims;                             /* number of dimensions of each point                                               */

double **pts;                           /* list of points of the current iteration of the algorithm                         */
double **ortho_array;                   /* list of ortogonal projections of the points in pts                               */
double **ortho_array_srt;               /* list of ortogonal projections of the point in pts to be sorted.                  */
double **pts_aux;                       /* list of points of the next iteration of the algorithm                            */

double *first_point;                    /* first point in the set i.e. with lower index relative to the initial point set   */

long n_points_local;                    /* number of points in the dataset present at this process                          */
long n_points_global;                   /* number of points in the dataset present at all processes                         */

double  *basub;                         /* point containing b-a for the orthogonal projections                              */
double *ortho_tmp;                      /* temporary pointer used for calculation the orthogonal projection                 */

node_ptr node_list;                     /* list of nodes of the ball tree                                                   */
double** node_centers;                  /* list of centers of the ball tree nodes                                           */

long n_nodes;                           /* number of nodes of the ball tree                                                 */
long node_id;                           /* id of the current node of the algorithm                                          */
long node_counter;                      /* number of nodes generated by the program                                         */

int rank;                               /* rank of the current process                                                      */
int n_procs;                            /* total number of processes                                                        */

long *processes_n_points;               /* array of the number of points owned by each process currently                    */

double **furthest_away_point_buffer;    /* buffer storing the local furthest away point at each process                      */

double *a;                              /* furthest away point from the first point in the globalset                         */
double *b;                              /* furthest away point from a in the global set                                      */
double *furthest_from_center;           /* furthest away point from center in the global set                                 */

/*
Returns the point in the global point set that is furthest away from point p
*/
void get_furthest_away_point(double *p, double *out) {
    double local_max_distance = 0.0;
    double *local_furthest_point = p;

    /*compute local furthest point from p*/
    for(long i = 0; i < n_points_local; i++){
        double curr_distance = distance(p, pts[i]);
        if(curr_distance > local_max_distance){
            local_max_distance = curr_distance;
            local_furthest_point = pts[i];
        }
    }

    /*get local furthest point from p of all processes*/
    MPI_Allgather(
                local_furthest_point,             /* send local furthest point to all other processes */
                n_dims,                           /* local furthest point has n_dims elements  */
                MPI_DOUBLE,                       /* each element is of type double */
                *furthest_away_point_buffer,      /* store each local furthest_away_point in the buffer */
                n_dims,                           /* each local_furthest_point has n_dims elements  */
                MPI_DOUBLE,                       /* each element is of type double */
                MPI_COMM_WORLD                    /* broadcast is all to all */
    );

    double global_max_distance = 0.0;
    double *global_furthest_point = p;

    /*of those, compute the furthest away from p*/
    for(int i = 0; i < n_procs; i++) {
        double curr_distance = distance(p, furthest_away_point_buffer[i]);
        if(curr_distance > global_max_distance){
            global_max_distance = curr_distance;
            global_furthest_point = furthest_away_point_buffer[i];
        }
    }
    copy_point(global_furthest_point, out);
}

/*
Returns the radius of the ball tree node defined by point center
*/
double get_radius(double* center) {
    get_furthest_away_point(center, furthest_from_center);
    return sqrt(distance(furthest_from_center, center));
}

/*
Used for quicksort
Compares the x coordenate of the two points
*/
int compare_node(const void* pt1, const void* pt2) {
    double* dpt1 = *((double**) pt1);
    double* dpt2 = *((double**) pt2);

    if(dpt1[0] > dpt2[0]) {
        return 1;
    }
    else if(dpt1[0] < dpt2[0]) {
        return -1;
    }
    else {
        return 0;
    }
}

/*
Returns the median projection of the dataset
by sorting the projections based on their x coordinate
*/
double* get_center() {
    memcpy(ortho_array_srt, ortho_array, sizeof(double*) * n_points_local);
    qsort(ortho_array_srt, n_points_local, sizeof(double*), compare_node);

    if(n_points_local % 2) { // is odd
        long middle = (n_points_local - 1) / 2;
        copy_point(ortho_array_srt[middle], node_centers[node_id]);
    }
    else { // is even
        long first_middle = (n_points_local / 2) - 1;
        long second_middle = (n_points_local / 2);

        middle_point(ortho_array_srt[first_middle], ortho_array_srt[second_middle], node_centers[node_id]);
    }
    return node_centers[node_id];
}

// TODO Clara get the nth point in the global set
// The owner broadcasts, the rest receives
void get_point(int n, double* out) {

}

/*
Computes the orthogonal projections of points in pts onto line defined by b-a
*/
void calc_orthogonal_projections(double* a, double* b) {
    sub_points(b, a, basub);
    for(long i = 0; i < n_points_local; i++){
        orthogonal_projection(basub, a, pts[i], ortho_array[i], ortho_tmp);
    }
}

/*
Places each point in pts in partition left or right by comparing the x coordinate
of its orthogonal projection with the x coordinate of the center point
*/
// TODO Bras rewrite, now we do not know how many points will be in left and right
void fill_partitions(double** left, double** right, double* center) {
    long l = 0;
    long r = 0;
    for(long i = 0; i < n_points_local; i++) {
        if(ortho_array[i][0] < center[0]) {
            left[l] = pts[i];
            l++;
        }
        else {
            right[r] = pts[i];
            r++;
        }
    }
}

/*void build_tree() {
    if(n_points_local == 1) {
        make_node(node_id, pts[0], 0, &node_list[node_id]);
        return;
    }

    double* a = get_furthest_away_point(pts[0]);
    double* b = get_furthest_away_point(a);

    calc_orthogonal_projections(a, b);

    double* center = get_center();
    double radius = get_radius(center);

    node_ptr node = make_node(node_id, center, radius, &node_list[node_id]);

    double **left = pts_aux;
    double **pts_aux_left = pts;
    double **ortho_array_left = ortho_array;
    double **ortho_array_srt_left = ortho_array_srt;
    long n_points_local_left = LEFT_PARTITION_SIZE(n_points_local);

    double **right = pts_aux + n_points_local_left;
    double **pts_aux_right = pts + n_points_local_left;
    double **ortho_array_right = ortho_array + n_points_local_left;
    double **ortho_array_srt_right = ortho_array_srt + n_points_local_left;
    long n_points_local_right = RIGHT_PARTITION_SIZE(n_points_local);

    long node_id_left = ++node_counter;
    long node_id_right = ++node_counter;

    fill_partitions(left, right, center);

    pts = left;
    pts_aux = pts_aux_left;
    ortho_array = ortho_array_left;
    ortho_array_srt = ortho_array_srt_left;
    n_points_local = n_points_local_left;
    node_id = node_id_left;
    build_tree();

    pts = right;
    pts_aux = pts_aux_right;
    ortho_array = ortho_array_right;
    ortho_array_srt = ortho_array_srt_right;
    n_points_local = n_points_local_right;
    node_id = node_id_right;
    build_tree();

    node->left_id = node_id_left;
    node->right_id = node_id_right;
}*/

/*
Get the number of points currently held by each process
*/
void get_processes_n_points() {
    long my_points = n_points_local;

    /*Broadcast all-to-all the number of points held locally*/
    MPI_Allgather(
                &my_points,             /*the address of the data the current process is sending*/
                1,                      /*number of data elements sent*/
                MPI_LONG,               /*type of data element sent*/
                processes_n_points,     /*the address where the current process stores the data received*/
                1,                      /*number of data elements sent by each process*/
                MPI_LONG,               /*type of data element received*/
                MPI_COMM_WORLD          /*sending and receiving to all processes*/
    );

#ifdef DEBUG
    for(int i = 0; i < n_procs; i++) {
        printf("%d processes_n_points[%d]=%ld\n", rank, i, processes_n_points[i]);
    }
#endif
}

/*
Returns the first point in the set
i.e. lower index relative to the initial set
*/
double *get_first_point() {
    int root = 0;
    /*first process that owns at least one point */
    for(int i = 0; i < n_procs; i++) {
        if(processes_n_points[i] != 0) {
            root = i;
            break;
        }
    }

    if(rank==root) {
        /*send*/
        MPI_Bcast(
                pts[0],             /*the address of the data the current process is sending*/
                n_dims,             /*the number of data elements sent*/
                MPI_DOUBLE,         /*type of data elements sent*/
                root,               /*rank of the process sending the data*/
                MPI_COMM_WORLD      /*broadcast to processes*/
        );
        return pts[0];
    }
    else {
        /*receive*/
        MPI_Bcast(
                first_point,        /*the address of the buffer the data received will be stored into*/
                n_dims,             /*the number of data elements received*/
                MPI_DOUBLE,         /*type of data elements received*/
                root,               /*rank of the process sending the data*/
                MPI_COMM_WORLD      /*broadcast to processes*/
        );
        return first_point;
    }
}

void build_node_mpi() {

    /**/
    get_processes_n_points();

    double* first_point = get_first_point();

#ifdef DEBUG
    printf("%d first_point=", rank);
    print_point(first_point);
#endif

    get_furthest_away_point(first_point, a);

    get_furthest_away_point(a, b);
#ifdef DEBUG
    printf("%d a=", rank);
    print_point(a);
    printf("%d b=", rank);
    print_point(b);
#endif

    //get_furthest_away_point of first point is a

    //get_furthest_away_point of a is b

    //calc_orthogonal_projections

    //sort projections

    //calc left and right points


}

void alloc_memory() {
    long n_points_ceil = (long) (ceil((double) (n_points_global) / (double) (n_procs)));

    n_points_local = BLOCK_SIZE(rank, n_procs, n_points_global);
    n_nodes = (n_points_global * 2) - 1;

    pts_aux = (double**) malloc(sizeof(double*) * n_points_ceil);

    ortho_array = create_array_pts(n_dims, n_points_ceil);
    ortho_array_srt = (double**) malloc(sizeof(double*) * n_points_ceil);

    basub = (double*) malloc(sizeof(double) * n_dims);
    ortho_tmp = (double*) malloc(sizeof(double) * n_dims);

    node_list = (node_ptr) malloc(sizeof(node_t) * n_nodes);
    node_centers = create_array_pts(n_dims, n_nodes);
    processes_n_points = (long*) malloc(sizeof(long) * n_procs);

    first_point = (double*) malloc(sizeof(double) * n_dims);

    furthest_away_point_buffer = create_array_pts(n_dims, n_procs);
    a = (double*) malloc(sizeof(double) * n_dims);
    b = (double*) malloc(sizeof(double) * n_dims);
    furthest_from_center = (double*) malloc(sizeof(double) * n_dims);
}

int main(int argc, char** argv) {
    double exec_time;
    exec_time = -omp_get_wtime();

    MPI_Init (&argc, &argv);
    MPI_Comm_rank (MPI_COMM_WORLD, &rank);
    MPI_Comm_size (MPI_COMM_WORLD, &n_procs);

    pts = get_points(argc, argv, &n_dims, &n_points_global);
    alloc_memory();

    build_node_mpi();
    /*
    build_tree();
    */

    exec_time += omp_get_wtime();
    /*
    if (!rank) {
        fprintf(stderr, "%.1lf\n", exec_time);
        printf("%d %ld\n", n_dims, n_nodes);
    }
    dump_tree();
    */
    MPI_Finalize();
}
