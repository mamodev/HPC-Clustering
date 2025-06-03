// #include <mpi.h>
// #include <iostream>
// #include <string>

// int main(int argc, char **argv)
// {
//     MPI_Init(&argc, &argv);

//     int world_rank, world_size;
//     MPI_Comm_size(MPI_COMM_WORLD, &world_size);
//     MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);


//     MPI_Comm workers;
//     MPI_Comm_split(MPI_COMM_WORLD, world_rank == 0 ? MPI_UNDEFINED : world_rank, world_rank, &workers);

//     if (world_rank == 0) {
//         std::string msg = "Hello world!";
//         for (size_t r = 1; r < world_size; ++r) {
//             MPI_Send(msg.c_str(), msg.size() + 1, MPI_CHAR, r, 0, MPI_COMM_WORLD);
//         }
        
//         char msg_buff[100];

//         for (int w = 0; w < world_size - 1; ++w) {
//             MPI_Request request;
//             MPI_Irecv(msg_buff, sizeof(msg_buff), MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &request);
//             MPI_Wait(&request, MPI_STATUS_IGNORE);
//         }

//     } else {

//         char msg[100];
//         MPI_Status status;
//         MPI_Recv(msg, sizeof(msg), MPI_CHAR, 0, 0, MPI_COMM_WORLD, &status);

//         int count;
//         MPI_Get_count(&status, MPI_CHAR, &count);


//         MPI_Barrier(workers);


//         MPI_Request request;
//         MPI_Isend(msg, count, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &request);
//         MPI_Wait(&request, MPI_STATUS_IGNORE);

//         MPI_Comm_free(&workers);
//     }
      
//     MPI_Finalize();

// }


#include <mpi.h>
#include <iostream>
#include <vector>
#include <string>

// A custom struct we’ll send with a derived datatype
struct Particle {
  int    id;
  double x, y, z;
};

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);

  int world_rank, world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

  //
  // 1) Broadcast a simple integer from rank 0 to all
  //
  int init_value = (world_rank == 0 ? 42 : 0);
  MPI_Bcast(&init_value, 1, MPI_INT, 0, MPI_COMM_WORLD);
  std::cout << "[" << world_rank << "] after Bcast, value = "
            << init_value << std::endl;

  //
  // 2) Scatter an array of ints from rank 0
  //
  const int N = 4;
  std::vector<int> scatter_data;
  if (world_rank == 0) {
    scatter_data.resize(N * world_size);
    for (int i = 0; i < N * world_size; ++i)
      scatter_data[i] = i;
  }
  std::vector<int> recv_sc(N);
  MPI_Scatter(scatter_data.data(), N, MPI_INT,
              recv_sc.data(), N, MPI_INT,
              0, MPI_COMM_WORLD);
  std::cout << "[" << world_rank << "] got from Scatter:";
  for (int v : recv_sc) std::cout << " " << v;
  std::cout << std::endl;

  //
  // 3) Allreduce (sum) across ranks
  //
  int local_sum = world_rank + 1;
  int global_sum = 0;
  MPI_Allreduce(&local_sum, &global_sum, 1, MPI_INT,
                MPI_SUM, MPI_COMM_WORLD);
  if (world_rank == 0)
    std::cout << "Allreduce sum = " << global_sum << std::endl;

  //
  // 4) Gather the scattered data back to rank 0
  //
  std::vector<int> gathered;
  if (world_rank == 0)
    gathered.resize(N * world_size);
  MPI_Gather(recv_sc.data(), N, MPI_INT,
             gathered.data(), N, MPI_INT,
             0, MPI_COMM_WORLD);
  if (world_rank == 0) {
    std::cout << "Gathered:";
    for (int v : gathered) std::cout << " " << v;
    std::cout << std::endl;
  }

  //
  // 5) Create and commit a derived datatype for Particle
  //
  MPI_Datatype particle_type;
  {
    Particle p;
    int          blocklen[2] = {1, 3};
    MPI_Aint     disp[2];
    MPI_Datatype types[2] = {MPI_INT, MPI_DOUBLE};
    MPI_Aint     base;
    MPI_Get_address(&p, &base);
    MPI_Get_address(&p.id, &disp[0]);
    MPI_Get_address(&p.x,  &disp[1]);
    disp[0] -= base;
    disp[1] -= base;
    MPI_Type_create_struct(2, blocklen, disp, types, &particle_type);
    MPI_Type_commit(&particle_type);
  }

  //
  // 6) Synchronous send (blocking) from rank 0 to rank 1
  //
  if (world_rank == 0 && world_size > 1) {
    std::string msg = "SyncSend Hello";
    MPI_Ssend(msg.c_str(), msg.size() + 1,
              MPI_CHAR, 1, 123, MPI_COMM_WORLD);
  }
  if (world_rank == 1) {
    char buf[64];
    MPI_Recv(buf, sizeof(buf), MPI_CHAR,
             0, 123, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    std::cout << "[1] got Ssend message: " << buf << std::endl;
  }

  //
  // 7) Asynchronous modes: Isend, Issend, Irsend
  //
  if (world_rank == 0 && world_size > 2) {
    char buf1[] = "Isend";
    char buf2[] = "Issend";
    char buf3[] = "Irsend";
    MPI_Request reqs[3];
    MPI_Isend(buf1, sizeof(buf1), MPI_CHAR,
              2, 200, MPI_COMM_WORLD, &reqs[0]);
    MPI_Issend(buf2, sizeof(buf2), MPI_CHAR,
               2, 201, MPI_COMM_WORLD, &reqs[1]);
    MPI_Irsend(buf3, sizeof(buf3), MPI_CHAR,
               2, 202, MPI_COMM_WORLD, &reqs[2]);
    MPI_Waitall(3, reqs, MPI_STATUSES_IGNORE);
  }
  if (world_rank == 2) {
    for (int tag = 200; tag <= 202; ++tag) {
      char buf[32];
      MPI_Recv(buf, sizeof(buf), MPI_CHAR,
               0, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      std::cout << "[2] got async(" << tag << "): " << buf
                << std::endl;
    }
  }

  //
  // 8) Send/Recv of the derived Particle type
  //
  if (world_rank == 0 && world_size > 1) {
    Particle p{99, 1.1, 2.2, 3.3};
    MPI_Send(&p, 1, particle_type,
             1, 300, MPI_COMM_WORLD);
  }
  if (world_rank == 1) {
    Particle pr;
    MPI_Recv(&pr, 1, particle_type,
             0, 300, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    std::cout << "[1] got Particle id=" << pr.id
              << " coords=(" << pr.x << ","
              << pr.y << "," << pr.z << ")" << std::endl;
  }

  //
  // 9) Alltoall exchange of one int per rank
  //
  int send_val = world_rank * 10;
  std::vector<int> alltoall_recv(world_size);
  MPI_Alltoall(&send_val, 1, MPI_INT,
               alltoall_recv.data(), 1, MPI_INT,
               MPI_COMM_WORLD);
  std::cout << "[" << world_rank << "] Alltoall recv:";
  for (int v : alltoall_recv) std::cout << " " << v;
  std::cout << std::endl;

  //
  // Cleanup
  //
  MPI_Type_free(&particle_type);
  MPI_Finalize();
  return 0;
}
