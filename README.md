"# ent"
#1. 配置项目
 假设机器上已经安装了cmake 3及visual studio 2010等开发工具，并安装了NSIS打包工具
mkdir -p build
cd build
cmake ../src
#2. 构建项目
cmake --build .
#3. 切换构建类型
cmake -D CMAKE_BUILD_TYPE=Release ..
#4. 使用ide
进入build目录，使用visual studio 2010打开ent.sln
