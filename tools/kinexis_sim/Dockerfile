FROM python:3.12-bookworm@sha256:4209fae09a3e20afd863e528bfddc235da5fd5977983e1996cd54cc9a72e6267
WORKDIR /sim
COPY bed.hpp bed.cpp gcode.hpp gcode.cpp gcode_main.cpp coupon.hpp coupon.cpp coupon_main.cpp scene.hpp scene.cpp scene_main.cpp main.cpp tests.cpp check_cli.py check_gcode.py check_coupon.py check_scene.py ./
COPY fixtures ./fixtures
RUN g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic bed.cpp main.cpp -o kinexis-sim \
 && g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic '-DFIXTURE_DIR="/sim/fixtures"' bed.cpp gcode.cpp tests.cpp -o kinexis-sim-tests \
 && g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic bed.cpp gcode.cpp gcode_main.cpp -o kinexis-gcode-check \
 && g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic bed.cpp coupon.cpp coupon_main.cpp -o kinexis-coupon \
 && g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic bed.cpp coupon.cpp scene.cpp scene_main.cpp -o kinexis-scene-check
CMD ["sh", "-c", "./kinexis-sim-tests && python check_cli.py ./kinexis-sim && python check_gcode.py ./kinexis-gcode-check && python check_coupon.py ./kinexis-coupon && python check_scene.py ./kinexis-scene-check"]
