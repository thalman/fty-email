/*
Copyright (C) 2014 Eaton

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

/*! \file subprocess.h
    \brief (sub) process C++ API
    \author Michal Vyskocil <MichalVyskocil@Eaton.com>
 */

#ifndef _SRC_SHARED_SUBPROCESS_H
#define _SRC_SHARED_SUBPROCESS_H

#include <cxxtools/posix/fork.h>

#include <climits>
#include <cstring>
#include <unistd.h>

#include <vector>
#include <deque>
#include <string>
#include <sstream>
#include <map>

/* \brief Helper classes for managing processes
 *
 * class SubProcess:
 * The advantage of this class is easyness of usage, as well as readability as
 * it handles several low-level oddities of POSIX/Linux C-API.
 *
 * Note that SubProcess instance is tied to one process only, so cannot be reused
 * to execute more than one subprocess. This is due "simulate" dynamic nature
 * of a processes. Therefor for a code running unspecified amount of processes,
 * instances must be heap allocated using new constructor.
 *
 * For that reason copy/move constructor and operator are disallowed.
 *
 * Example:
 * \code
 * SubProcess proc("/bin/true");
 * proc.run();
 * proc.wait();
 * std::cout "process pid: " << proc.getPid() << std::endl;
 * \endcode
 *
 * class ProcessQue:
 * This maintains a queue of a processes. There are three queues: incomming,
 * running and done. Those are updated only at schedule() call, when finished
 * processes are moved to done and those from the incomming queue are started
 * and moved to running.
 *
 * class ProcCache:
 * Maintains stdout/stderr output for a process in
 * std::ostringstream. It is helper class for
 *
 * class ProcCacheMap:
 * Maintain a cache for set of processes - each is identified by pid
 * (/todo think about const SubProcess *). It stores output of several
 * processes run and poll'ed in parallel.
 */


//! \brief list of arguments
typedef std::vector<std::string> Argv;

class SubProcess {
    public:

        static const int STDIN_PIPE=0x01;
        static const int STDOUT_PIPE=0x02;
        static const int STDERR_PIPE=0x04;

        static const int PIPE_DEFAULT = -1;
        static const int PIPE_DISABLED = -2;

        // \brief construct instance
        //
        // @param argv  - C-like string of argument, see execvpe(2) for details
        // @param flags - controll the creation of stdin/stderr/stdout pipes, default no
        //
        // \todo TODO does not deal with a command line limit
        explicit SubProcess(Argv cxx_argv, int flags=0);

        // \brief gracefully kill/terminate the process and close all pipes
        virtual ~SubProcess();

        // \brief return the commandline
        const Argv& argv() const { return _cxx_argv; }

        // \brief return the commandline as a space delimited string
        std::string argvString() const;

        //! \brief return pid of executed command
        pid_t getPid() const { return _fork.getPid(); }

        //! \brief get the pipe ends connected to stdin of started program, or -1 if not started
        int getStdin() const { return _inpair[1]; }

        //! \brief get the pipe ends connected to stdout of started program, or -1 if not started
        int getStdout() const { return _outpair[0]; }

        //! \brief get the pipe ends connected to stderr of started program, or -1 if not started
        int getStderr() const { return _errpair[0]; }

        //! \brief returns last checked status of the process
        bool isRunning() { poll(); return _state == SubProcessState::RUNNING; }

        //! \brief get the return code, \see wait for meaning
        int getReturnCode() const { return _return_code; }

        //! \brief return core dumped flag
        bool isCoreDumped() const { return _core_dumped; }

        // \brief creates a pipe/pair for stdout/stderr, fork and exec the command. Note this
        // can be started only once, all subsequent calls becames nooop and return true.
        //
        // @return true if exec was successfull, otherwise false and reason is in errno
        bool run();

        //! \brief wait on program terminate
        //
        //  @param no_hangup if false (default) wait indefinitelly, otherwise return immediatelly
        //  @return positive return value of a process
        //          negative is a number of a signal which terminates process
        int wait(bool no_hangup=false);

        //! \brief wait on process for defined timeout [s]
        //
        //  @param timeout[s] wait for process
        //  @return positive return value of a process
        //          negative is a number of a signal which terminates process
        int wait(unsigned int timeout);

        //! \brief no hanging variant of /see wait
        int poll() {  return wait(true); }

        //! \brief kill the subprocess with defined signal, default SIGTERM/15
        //
        //  @param signal - signal, defaul is SIGTERM
        //
        //  @return see kill(2)
        int kill(int signal=SIGTERM);

        //! \brief terminate the subprocess with SIGKILL/9
        //
        //  This calls wait() to ensure we are not creating zombies
        //
        //  @return \see kill
        int terminate();

        const char* state() const;

    protected:

        enum class SubProcessState {
            NOT_STARTED,
            RUNNING,
            FINISHED
        };

        cxxtools::posix::Fork _fork;
        SubProcessState _state;
        Argv _cxx_argv;
        int _return_code;
        bool _core_dumped;
        int _inpair[2];
        int _outpair[2];
        int _errpair[2];

        // disallow copy and move constructors
        SubProcess(const SubProcess& p) = delete;
        SubProcess& operator=(SubProcess p) = delete;
        SubProcess(const SubProcess&& p) = delete;
        SubProcess& operator=(SubProcess&& p) = delete;

};

// \brief read all things from file descriptor
//
// try to read as much as possible from file descriptor and return it as std::string
std::string read_all(int fd);

// \brief read all things from file descriptor while compensating for dealys
//
// Try to read as much as possible from file descriptor and return it as
// std::string. But waits for the first string to appear (5s max) and reads
// till the input stops for more than 1ms
std::string wait_read_all(int fd);

// \brief Run command with arguments.  Wait for complete and return the return value.
//
// @return see \SubProcess.wait
int call(const Argv& args);

// \brief Run command with arguments and return its output as a string.
//
// @param args list of command line arguments
// @param o reference to variable will contain stdout
// @param e reference to variable will contain stderr
// @param timeout of the process (0 = no timeout, wait forever)
// @return see \SubProcess.wait for meaning
//
// \warning use only for commands producing less than default pipe capacity (65536 on Linux).
//          Otherwise this call would be blocked indefinitelly.
 int output(const Argv& args, std::string& o, std::string& e, unsigned int timeout = 0);

// \brief Run command with arguments and input on stdin and return its output as a string.
//
// @param args list of command line arguments
// @param o reference to variable will contain stdout
// @param e reference to variable will contain stderr
// @param i const reference to variable will contain stdin
// @param timeout of the process (0 = no timeout, wait forever)
// @return see \SubProcess.wait for meaning
//
// \warning use only for commands producing less than default pipe capacity (65536 on Linux).
//          Otherwise this call would be blocked indefinitelly.
int
output(
    const Argv& args,
    std::string& o,
    std::string& e,
    const std::string& i,
    unsigned int timeout = 0);

void subprocess_test (bool verbose);
#endif //_SRC_SHARED_SUBPROCESS_H

