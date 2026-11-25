FROM python:3.12-bookworm@sha256:4209fae09a3e20afd863e528bfddc235da5fd5977983e1996cd54cc9a72e6267
WORKDIR /sim
COPY bed.hpp bed.cpp main.cpp tests.cpp check_cli.py ./
COPY fixtures ./fixtures
RUN g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic bed.cpp main.cpp -o kinexis-sim \
 && g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic '-DFIXTURE_DIR="/sim/fixtures"' bed.cpp tests.cpp -o kinexis-sim-tests
CMD ["sh", "-c", "./kinexis-sim-tests && python check_cli.py ./kinexis-sim"]
