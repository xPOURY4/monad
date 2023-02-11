# tkvdb perf test

download the code
```bash
cd ~/git
# clone the tkvdb source code
git clone git@github.com:vmxdev/tkvdb.git
# in monad-trie
cd monad-trie
git submodule update --init --recursive
```

build the perf test script
```bash
# set env var
export PATH_TKVDB=~/git/tkvdb

# build the project under monad-trie/
mkdir build
cd build
cmake ..
make -j

# run test
./test/tkvdb_perf_test
```

perf the program
```bash
# perf record -g --call-graph --pid=$(PID)
./tkvdb_perf_test && perf record -g --call-graph=dwarf --pid=$(ps -u $USER | grep -i tkvdb_perf_test | head -n 1 | awk {'print $1'})

perf report -g
```
