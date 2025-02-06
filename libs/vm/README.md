# Monad EVM Compiler

> [!WARNING]  
> This project is an untested work in progress.

An implementation of an [EVMC][evmc]-compatible implementation of the Ethereum
Virtual Machine, using LLVM to compile EVM bytecode to native code for faster
execution. The work in this project is a C++ reimplementation of the
[`monad-jit`][jit] project, with the eventual aim of merging the compiler into
the core Monad monorepo.

## Building & Running

The compiler is built and tested in CI on Ubuntu 24.04. To build locally, first
install the following dependencies via `apt`:
```
build-essential
cmake
llvm-18-dev
libbenchmark-dev
libcli11-dev
libgmock-dev
libgtest-dev
```

Then, from the project root:
```console
$ git submodule update --init --recursive
$ cmake -S . -B build
$ cmake --build build
```

## Testing

### Ethereum Tests

The ethereum blockchain tests can be executed with the command
```
$ build/src/test/blockchain/blockchain-tests
```
It will implicitly skip blockchain tests which
* contain invalid blocks and
* contain unexpected json format and
* execute slowly.

Use the `--gtest_filter` flag to enable or disable specific tests. See
```
$ build/src/test/blockchain/blockchain-tests --help
```

### Debugging Ethereum Tests

Environment variables can be used for debugging failing tests.

By setting the `MONAD_BLOCKCHAIN_TEST_DEBUG_DIR` environment
variable to a valid directory, the compiler will print contract
assembly code in files to this directory. The resulting assembly
code will contain calls to a runtime function, which prints gas
remaining for each basic block entered. For example,
```
$ export MONAD_BLOCKCHAIN_TEST_DEBUG_DIR=/tmp/debug
$ build/src/test/blockchain/blockchain-tests --gtest_filter="*.jumpiNonConst"
```
will print something like
```
...
Block 0x00: gas remaining: 379000
Block 0x2d: gas remaining: 378784
Block 0x00: gas remaining: 379000
...
```
and create assembly file with name the address of the called contract
```
$ cat /tmp/debug/095E7BAEA6A6C7C4C2DFEB977EFAC326AF552D87
push rbp
push rbx
push r12
push r13
push r14
push r15
mov rbx, rdi
mov rbp, rsi
mov [rbx+280], rsp
sub rsp, 56
mov qword ptr [rsp+48], 0
//   0x00:
//       PUSH20 0x95e7baea6a6c7c4c2dfeb977efac326af552d87
//       BALANCE
//       PUSH20 0x95e7baea6a6c7c4c2dfeb977efac326af552d87
//       BALANCE
//     JumpI 1
lea rdi, qword ptr [L5]
mov rsi, rbx
call qword ptr [L6]
cmp qword ptr [rsp+48], 1022
ja OutOfBounds
...
```

To run the tests using evmone instead of monad-compiler, define
the `EVMONE_VM_ONLY` environment variable. For instance
```
$ export EVMONE_VM_ONLY=1
$ build/src/test/blockchain/blockchain-tests
```

If the project is configured with `EVMONE_DEBUG_MODE` enabled, e.g.
```
$ cmake -S . -B build -DEVMONE_DEBUG_MODE=ON
```
then one can define the `EVMONE_DEBUG_MODE` environment variable
to get runtime debug information from evmone and the test host,
defined in the evmone project. For example
```
$ unset EVMONE_VM_ONLY
$ export MONAD_BLOCKCHAIN_TEST_DEBUG_DIR=/tmp/debug
$ export EVMONE_DEBUG_MODE=1
$ build/src/test/blockchain/blockchain-tests \
    --gtest_filter="*.jumpiNonConst"
...
offset: 0x59  opcode: 0x90  gas_left: 29977858
offset: 0x5a  opcode: 0x62  gas_left: 29977855
offset: 0x5e  opcode: 0x1  gas_left: 29977852
offset: 0x5f  opcode: 0x55  gas_left: 29977849
offset: 0x60  opcode: 0x0  gas_left: 29975649
START baseline_execute address 095E7BAEA6A6C7C4C2DFEB977EFAC326AF552D87 with gas = 379000
Block 0x00: gas remaining: 379000
Block 0x2d: gas remaining: 378784
END baseline_execute address 095E7BAEA6A6C7C4C2DFEB977EFAC326AF552D87
offset: 0x00  opcode: 0x33  gas_left: 30000000
offset: 0x01  opcode: 0x73  gas_left: 29999998
...
offset: 0x16  opcode: 0x14  gas_left: 29999995
offset: 0x5f  opcode: 0x55  gas_left: 29977849
offset: 0x60  opcode: 0x0  gas_left: 29975649
START baseline_execute address 095E7BAEA6A6C7C4C2DFEB977EFAC326AF552D87 with gas = 379000
Block 0x00: gas remaining: 379000
END baseline_execute address 095E7BAEA6A6C7C4C2DFEB977EFAC326AF552D87
```

Note that evmone is being used for executing certain contracts, even when
the `EVMONE_VM_ONLY` environment variable is not defined. For example,
`CREATE` and `CREATE2` calls are always executed with evmone.

The lines starting with `offset` contain runtime debug information from
evmone, with one line for each instruction executed by evmone. The
`START` and `END` lines contain information about which contracts are
executing. Lines starting with `Block` contain runtime debug output
from `monad-compiler`.

### Directory Type Check Test

After building the compiler source code, the `directory-type-check` executable
can be used on a directory containing bytecode contracts. It will recursively
traverse the directory and run the type inference algorithm on all the
contracts. It will additionally run the type checking algorithm to verify the
correctness of the inferred types. For example
```console
build/src/test/utils/directory-type-check my/contracts-dir
```
will print type inference errors to standard error and print contract type
information to standard out. If it prints to standard error, there is bug
somewhere.

## Structure

There are four main components:
- A compiler from EVM bytecode to LLVM IR; the programs generated by this
  compiler rely on being linked against a runtime support library, and on being
  run in the context of an EVMC-compatible host.
- A runtime support library for EVM programs that handles interaction with the
  host context (gas calculations etc.).
- An EVMC-compatible VM layer that handles compiling bytecode to LLVM, then
  executing the compiled code via LLVM's JIT implementation.
- A debugging tool that compiles a contract to LLVM IR and dumps the result to
  `stdout`.

## Documentation

To build the project's documentation using [Doxygen][doxygen], run:
```console
$ doxygen
```
from the project root.

On Peach machines, the built documentation needs to be accessed over SSH. To run
a local HTTP server that can be tunneled to your local machine, run:
```console
$ ./scripts/serve-docs.sh
```

Then, on your local machine open an HTTP tunnel over SSH with:
```console
$ ssh -N -L 8000:localhost:8000 <user@some-peach>
```

The documentation is then accessible from `http://localhost:8000` locally.

[doxygen]: https://www.doxygen.nl/
[evmc]: https://github.com/ethereum/evmc
[jit]: https://github.com/monad-crypto/monad-jit
[hunter]: https://github.com/ethereum/evmc/pull/169

## Coverage
To enable coverage statistics set `-DCOVERAGE=ON` when configuring, e.g.
```console
$ cmake -DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=ON -S . -B build -G Ninja
```
After running the desired executable, use `gcovr .` to gather coverage statistics.

## Linting & Formatting

To run the linter, call:
```console
scripts/apply-clang-tidy-fixes.sh build run-clang-tidy-18
```

To apply the formatter, use:
```console
find libs -iname '*.h' -o -iname '*.cpp' | xargs clang-format-18 -i
find test -iname '*.h' -o -iname '*.cpp' | xargs clang-format-18 -i
```
