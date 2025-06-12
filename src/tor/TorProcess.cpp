/* Ricochet - https://ricochet.im/
 * Copyright (C) 2014, John Brooks <john.brooks@dereferenced.net>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 *    * Neither the names of the copyright owners nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

#include "util/rsdir.h"
#include "util/rsfile.h"
#include "pqi/pqifdbin.h"

#include "TorProcess.h"
#include "CryptoKey.h"

#ifdef WINDOWS_SYS
#include "util/rsstring.h"

#include <fcntl.h>
#define pipe(fds) _pipe(fds, 1024, _O_BINARY)
#endif

using namespace Tor;

static const int INTERVAL_BETWEEN_CONTROL_PORT_READ_TRIES = 5; // try every 5 secs.

TorProcess::TorProcess(TorProcessClient *client)
    : m_client(client),  mState(TorProcess::NotStarted), mControlPort(0), mLastTryReadControlPort(0),mVerbose(false)
{
    mControlPortReadNbTries=0;

}

TorProcess::~TorProcess()
{
    if (state() > NotStarted)
        stop();
}

void TorProcess::setVerbose(bool v)
{
    mVerbose = v;
}
std::string TorProcess::executable() const
{
    return mExecutable;
}

void TorProcess::setExecutable(const std::string &path)
{
    mExecutable = path;
}

std::string TorProcess::dataDir() const
{
    return mDataDir;
}

void TorProcess::setDataDir(const std::string &path)
{
    mDataDir = path;
}

std::string TorProcess::defaultTorrc() const
{
    return mDefaultTorrc;
}

void TorProcess::setDefaultTorrc(const std::string &path)
{
    mDefaultTorrc = path;
}

std::list<std::string> TorProcess::extraSettings() const
{
    return mExtraSettings;
}

void TorProcess::setExtraSettings(const std::list<std::string> &settings)
{
    mExtraSettings = settings;
}

TorProcess::State TorProcess::state() const
{
    return mState;
}

std::string TorProcess::errorMessage() const
{
    return mErrorMessage;
}

// Does a popen, but dup all file descriptors (STDIN STDOUT and STDERR) to the
// FDs supplied by the parent process

int popen3(int fd[3],const std::vector<std::string>& args,TorProcessHandle& pid)
{
    RsInfo() << "  Launching Tor in background..." ;

    int i, e;
    int p[3][2];
    // set all the FDs to invalid
    for(i=0; i<3; i++)
        p[i][0] = p[i][1] = -1;
    // create the pipes
    for(int i=0; i<3; i++)
        if(pipe(p[i]))
            goto error;

#ifdef WINDOWS_SYS
    // Set up members of the PROCESS_INFORMATION structure.
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    // Set up members of the STARTUPINFO structure.
    // This structure specifies the STDIN and STDOUT handles for redirection.
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    si.hStdInput = (HANDLE) _get_osfhandle(p[STDIN_FILENO][0]);
    si.hStdOutput = (HANDLE) _get_osfhandle(p[STDOUT_FILENO][1]);
    si.hStdError = (HANDLE) _get_osfhandle(p[STDERR_FILENO][1]);
    si.wShowWindow = SW_HIDE;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;

    if (si.hStdInput != INVALID_HANDLE_VALUE &&
        si.hStdOutput != INVALID_HANDLE_VALUE &&
        si.hStdError != INVALID_HANDLE_VALUE) {
        // build commandline
        std::string cmd;
        for (std::vector<std::string>::const_iterator it = args.begin(); it != args.end(); ++it) {
            if (it != args.begin()) {
                cmd += " ";
            }
            cmd += *it;
        }

        std::wstring wcmd;
        if (!librs::util::ConvertUtf8ToUtf16(cmd, wcmd)) {
            goto error;
        }

        WINBOOL success = CreateProcess(nullptr,
              (LPWSTR) wcmd.c_str(), // command line
              nullptr,               // process security attributes
              nullptr,               // primary thread security attributes
              TRUE,                  // handles are inherited
              0,                     // creation flags
              nullptr,               // use parent's environment
              nullptr,               // use parent's current directory
              &si,                   // STARTUPINFO pointer
              &pi);                  // receives PROCESS_INFORMATION

        if (success) {
            pid = pi.hProcess;

            CloseHandle(pi.hThread);

            fd[STDIN_FILENO] = p[STDIN_FILENO][1];
            close(p[STDIN_FILENO][0]);
            fd[STDOUT_FILENO] = p[STDOUT_FILENO][0];
            close(p[STDOUT_FILENO][1]);
            fd[STDERR_FILENO] = p[STDERR_FILENO][0];
            close(p[STDERR_FILENO][1]);

            // success
            return 0;
        }
    }

    // fall through error

#else
    {
        const char *arguments[args.size()+1];
        int n=0;

        // We first pushed everything into a vector of strings to save the pointers obtained from string returning methods
        // by the time the process is launched.

        for(uint32_t i=0;i<args.size();++i)
            arguments[n++]= args[i].data();

        arguments[n] = nullptr;

        // and fork
        pid = fork();
        if(-1 == pid)
            goto error;
        // in the parent?
        if(pid)
        {
            // parent
            fd[STDIN_FILENO] = p[STDIN_FILENO][1];
            close(p[STDIN_FILENO][0]);
            fd[STDOUT_FILENO] = p[STDOUT_FILENO][0];
            close(p[STDOUT_FILENO][1]);
            fd[STDERR_FILENO] = p[STDERR_FILENO][0];
            close(p[STDERR_FILENO][1]);
            // success
            return 0;
        }
        else
        {
            RsInfo() << "  Launching sub-process..." ;
            // child
            dup2(p[STDIN_FILENO][0],STDIN_FILENO);
            close(p[STDIN_FILENO][1]);
            dup2(p[STDOUT_FILENO][1],STDOUT_FILENO);
            close(p[STDOUT_FILENO][0]);
            dup2(p[STDERR_FILENO][1],STDERR_FILENO);
            close(p[STDERR_FILENO][0]);

            // here we try and run it

            execv(*arguments,const_cast<char*const*>(arguments));

            // if we are there, then we failed to launch our program
            perror("Could not launch");
            fprintf(stderr," \"%s\"\n",*arguments);
        }
    }
#endif

error:
    e = errno;
    // preserve original error
    RsErr() << "An error occurred while trying to launch tor in background." ;
    for(i=0; i<3; i++) {
        close(p[i][0]);
        close(p[i][1]);
    }
    errno = e;
    return -1;
}

void TorProcess::start()
{
    if (state() > NotStarted)
        return;

    mErrorMessage.clear();

    if (mExecutable.empty() || mDataDir.empty()) {
        mErrorMessage = "Tor executable and data directory not specified";
        mState = Failed;

        if(m_client) m_client->processStateChanged(mState); // emit stateChanged(d->state);
        if(m_client) m_client->processErrorChanged(mErrorMessage); // emit errorMessageChanged(d->errorMessage);
        return;
    }

    if (!ensureFilesExist()) {
        mState = Failed;
        if(m_client) m_client->processErrorChanged(mErrorMessage);// emit errorMessageChanged(d->errorMessage);
        if(m_client) m_client->processStateChanged(mState);// emit stateChanged(d->state);
        return;
    }

    ByteArray password = controlPassword();
    ByteArray hashedPassword = torControlHashedPassword(password);

    if (password.empty() || hashedPassword.empty()) {
        mErrorMessage = "Random password generation failed";
        mState = Failed;
        if(m_client) m_client->processErrorChanged(mErrorMessage);// emit errorMessageChanged(d->errorMessage);
        if(m_client) m_client->processStateChanged(mState); // emit stateChanged(d->state);
    }
    else if(mVerbose)
        RsDbg() << "Using ControlPasswd=\"" << password.toString() << "\", hashed version=\"" << hashedPassword.toString() << "\"" ;

    mState = Starting;

    if(m_client) m_client->processStateChanged(mState);// emit stateChanged(d->state);

    if (RsDirUtil::fileExists(controlPortFilePath()))
        RsDirUtil::removeFile(controlPortFilePath());

    mControlPort = 0;
    mControlHost.clear();

    // Launch the process

    std::vector<std::string> args;

    args.push_back(mExecutable);

    if (!mDefaultTorrc.empty())
    {
        args.push_back("--defaults-torrc");
        args.push_back(mDefaultTorrc);
    }

    args.push_back("-f");
    args.push_back(torrcPath());

    args.push_back("DataDirectory") ;
    args.push_back(mDataDir);

    args.push_back("HashedControlPassword") ;
    args.push_back(hashedPassword.toString());

    args.push_back("ControlPort") ;
    args.push_back("auto");

    args.push_back("ControlPortWriteToFile");
    args.push_back(controlPortFilePath());

    args.push_back("__OwningControllerProcess") ;
    args.push_back(RsUtil::NumberToString(getpid()));

    for(auto s:mExtraSettings)
        args.push_back(s);

    int fd[3];  // File descriptors array

    if(popen3(fd,args,mTorProcessId))
    {
        RsErr() << "Could not start Tor process. errno=" << errno ;
        mState = Failed;
        return;	// stop the control thread
    }

    RsFileUtil::set_fd_nonblock(fd[STDOUT_FILENO]);
    RsFileUtil::set_fd_nonblock(fd[STDERR_FILENO]);

    mStdOutFD = new RsFdBinInterface(fd[STDOUT_FILENO], false);
    mStdErrFD = new RsFdBinInterface(fd[STDERR_FILENO], false);
}

void TorProcess::tick()
{
    mStdOutFD->tick();
    mStdErrFD->tick();

    unsigned char buff[1024];
    int s;

    if((s=mStdOutFD->readline(buff,1024))) logMessage(std::string((char*)buff,s));
    if((s=mStdErrFD->readline(buff,1024))) logMessage(std::string((char*)buff,s));

    if(!mStdOutFD->isactive() && !mStdErrFD->isactive())
    {
        static rstime_t last(0);

        rstime_t now = time(nullptr);
        if(now > last + 10)
        {
            last = now;
            RsErr() << "Tor process died. Exiting TorControl process." ;
        }
        stop();
        return;
    }
    time_t now = time(nullptr);

    if(mControlPortReadNbTries <= 10 && (mControlPort==0 || mControlHost.empty()) && mLastTryReadControlPort + INTERVAL_BETWEEN_CONTROL_PORT_READ_TRIES < now)
    {
        mLastTryReadControlPort = now;

        if(tryReadControlPort())
        {
            mState = Ready;
            m_client->processStateChanged(mState);// stateChanged(mState);
        }
        else if(mControlPortReadNbTries > 10)
        {
            mState = Failed;
            m_client->processStateChanged(mState);// stateChanged(mState);
        }
    }
}

void TorProcess::stop()
{
    if (state() < Starting)
        return;

    while(mState == Starting)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

#ifdef WINDOWS_SYS
    TerminateProcess (mTorProcessId, 0);
#else
    kill(mTorProcessId,SIGTERM);
#endif

    RsInfo() << "Tor process has been normally terminated. Exiting.";

    mState = NotStarted;

    if(m_client) m_client->processStateChanged(mState);// emit stateChanged(d->state);
}

void TorProcess::stateChanged(int newState)
{
    if(m_client)
        m_client->processStateChanged(newState);
}
void TorProcess::errorMessageChanged(const std::string& errorMessage)
{
    if(m_client)
        m_client->processErrorChanged(errorMessage);
}
void TorProcess::logMessage(const std::string& message)
{
    if(m_client)
        m_client->processLogMessage(message);
}

ByteArray TorProcess::controlPassword()
{
    if (mControlPassword.empty())
        mControlPassword = RsRandom::printable(16);

    return mControlPassword;
}

std::string TorProcess::controlHost()
{
    return mControlHost;
}

unsigned short TorProcess::controlPort()
{
    return mControlPort;
}

bool TorProcess::ensureFilesExist()
{
    if(!RsDirUtil::checkCreateDirectory(mDataDir))
    {
        mErrorMessage = "Cannot create Tor data directory: " + mDataDir;
        return false;
    }

    if (!RsDirUtil::fileExists(torrcPath()))
    {
        FILE *f = RsDirUtil::rs_fopen(torrcPath().c_str(),"w");

        if(!f)
        {
            mErrorMessage = "Cannot create Tor configuration file: " + torrcPath();
            return false;
        }
        else
            fclose(f);
    }

    return true;
}

std::string TorProcess::torrcPath() const
{
    return RsDirUtil::makePath(mDataDir,"torrc");
}

std::string TorProcess::controlPortFilePath() const
{
    return RsDirUtil::makePath(mDataDir,"control-port");
}

bool TorProcess::tryReadControlPort()
{
    FILE *file = RsDirUtil::rs_fopen(controlPortFilePath().c_str(),"r");
    RsInfo() << "  Trying to read control port" ;

    if(file)
    {
        char *line = nullptr;
        size_t tmp_buffsize = 0;

        size_t size = RsFileUtil::rs_getline(&line,&tmp_buffsize,file);
        ByteArray data = ByteArray((unsigned char*)line,size).trimmed();
        free(line);

        fclose(file);

        int p;
        if (data.startsWith("PORT=") && (p = data.lastIndexOf(':')) > 0) {
            mControlHost = data.mid(5, p - 5).toString();
            mControlPort = data.mid(p+1).toInt();

            if (!mControlHost.empty() && mControlPort > 0)
            {
                RsInfo() << "  Got control host/port = " << mControlHost << ":" << mControlPort ;
                return true;
            }
        }
    }
    return false;
}
