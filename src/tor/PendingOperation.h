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

#pragma once

#include <functional>
#include <string>

/* Represents an asynchronous operation for reporting status
 *
 * This class is used for asynchronous operations that report a
 * status and errors when finished, particularly for exposing them
 * to QML.
 *
 * Subclass PendingOperation to implement your operation's logic.
 * You also need to handle the object's lifetime, for example by
 * calling deleteLater() when finished() is emitted.
 *
 * PendingOperation will emit finished() and one of success() or
 * error() when completed.
 */
class PendingOperation
{
public:
    PendingOperation();

    bool isFinished() const;
    bool isSuccess() const;
    bool isError() const;
    std::string errorMessage() const;

    void finishWithError(const std::string &errorMessage);
    void finishWithSuccess();

    void set_finished_callback(const std::function<void(void)>& f) { mFinishedCallback = f; }
private:
    bool m_finished;
    std::string m_errorMessage;

    std::function<void(void)> mFinishedCallback;
    std::function<void(void)> mSuccessCallback;
    std::function<void(const std::string&)> mErrorCallback;

};

