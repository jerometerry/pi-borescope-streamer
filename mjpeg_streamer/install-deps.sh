mkdir -p third_party
git clone --depth 1 --branch v20.67.0 https://github.com/uNetworking/uWebSockets.git third_party/uWebSockets
git clone --depth 1 --branch v0.8.8 https://github.com/uNetworking/uSockets.git third_party/uSockets
cd third_party/uSockets
make clean
make CC="zig cc" CXX="zig c++" AR="zig ar" WITH_LTO=0
cd ../../