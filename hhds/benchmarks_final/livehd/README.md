LiveHD benchmark slot
=====================

This checkout does not contain a buildable LiveHD/lgraph source tree or target.
The final benchmark scripts still create `livehd/results.csv` and the comparison
CSV keeps a `livehd` column, but values are `N/A` until a real LiveHD benchmark
binary is added here.

Do not fake LiveHD timings with another library. Once the real API is available,
add a `livehd_final_bench` binary that writes the same raw CSV schema as the
HHDS and Boost binaries.

