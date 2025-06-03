#pragma once 


#include <mpi.h>    

#define MPI_PERF_WRAPPER

#if defined(__GNUC__) || defined(__clang__)
#  define NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#  define NOINLINE __declspec(noinline)
#else
#  define NOINLINE
#endif

#if defined(MPI_PERF_WRAPPER)

int NOINLINE mpi_send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) {
    int result = MPI_Send(buf, count, datatype, dest, tag, comm);
    return result;
}

int NOINLINE mpi_recv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Status *status) {
    int result = MPI_Recv(buf, count, datatype, source, tag, comm, status);
    return result;
}

int NOINLINE mpi_isend(const void *buf, int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm, MPI_Request *request) {
    int result = MPI_Isend(buf, count, datatype, dest, tag, comm, request);
    return result;
}

int NOINLINE mpi_irecv(void *buf, int count, MPI_Datatype datatype, int source, int tag, MPI_Comm comm, MPI_Request *request) {
    int result = MPI_Irecv(buf, count, datatype, source, tag, comm, request);
    return result;
}

int NOINLINE mpi_waitsome(int incount, MPI_Request array_of_requests[], int *outcount, int array_of_indices[], MPI_Status array_of_statuses[]) {
    int result = MPI_Waitsome(incount, array_of_requests, outcount, array_of_indices, array_of_statuses);
    return result;
}

int NOINLINE mpi_test_cancelled(const MPI_Status *status, int *flag) {
    int result = MPI_Test_cancelled(status, flag);
    return result;
}

#else

#define mpi_send MPI_Send
#define mpi_recv MPI_Recv
#define mpi_isend MPI_Isend
#define mpi_irecv MPI_Irecv
#define mpi_waitsome MPI_Waitsome
#define mpi_test_cancelled MPI_Test_cancelled

#endif
