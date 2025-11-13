/*[λ] the photon parsing interface*/
#include "parse.hpp"
/*[λ] the photon parsing interface*/
#include "parse.hpp"


// layout(local_size_x = 4) in; // 4 threads, one per row

// // Input 
// la
// layoutt(binding = 0) buffer InputMatrix {
//     float A[16];
// };

// layout(binding = 1) buffer OutpPutMatrix {
//     float L[16];
// };

// void main() {
//     uint row = gl_GlobalInvocationID.x; // Thread-per-row!!


//     const uint N = 4;

//     for (uint col = 0; col < N; col++) {

//         if (row < col) {

//         }

//         if (row == col) {
//             // Diagonal shii element
//             L[row * N + col] = sqrt(A[row * N + col] - sum);
//         }
//         else {

//             L[row * N + col] = (A[row * N + col] - sum) / L[col * N + col];
//         }
//         barrier(); // Synchronize? threads in this workgroup
//     }
// } womp womp