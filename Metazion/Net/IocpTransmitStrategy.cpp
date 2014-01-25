#include "Metazion/Net/IocpTransmitStrategy.hpp"

#include "Metazion/Net/TransmitSocket.hpp"

#if defined(MZ_PLATFORM_WINOWS)

DECL_NAMESPACE_MZ_NET_BEGIN

IocpTransmitStrategy::IocpTransmitStrategy(TransmitSocket& transmitSocket)
    : m_transmitSocket(transmitSocket) {}

IocpTransmitStrategy::~IocpTransmitStrategy() {}

void IocpTransmitStrategy::Reset() {
    m_sendOperation.Reset();
    m_recvOperation.Reset();
}

bool IocpTransmitStrategy::IsBusy() const {
    bool ret = m_sendOperation.IsBusy();
    if (ret) {
        return true;
    }

    ret = m_recvOperation.IsBusy();
    if (ret) {
        return true;
    }

    return false;
}

bool IocpTransmitStrategy::PostInputOperation() {
    if (!m_transmitSocket.IsReady()) {
        return false;
    }

    if (m_recvOperation.IsBusy()) {
        return true;
    }

    m_recvOperation.SetBusy(true);

   return _PostInputOperation();
}

bool IocpTransmitStrategy::PostOutputOperation() {
    if (!m_transmitSocket.IsReady()) {
        return false;
    }

    if (m_sendOperation.IsBusy()) {
        return true;
    }

    if (!m_transmitSocket.GetSocketBuffer().HasDataToSend()) {
        return true;
    }

    m_sendOperation.SetBusy(true);

    return _PostOutputOperation();
}

bool IocpTransmitStrategy::HandleSuccessOperation(const IocpOperation* iocpOperation
    , DWORD byteNumber) {
    switch (iocpOperation->m_type) {
    case IocpOperation::TYPE_RECV:
        return HandleInputSuccessOperation(iocpOperation, byteNumber);
    case IocpOperation::TYPE_SEND:
        return HandleOutputSuccessOperation(iocpOperation, byteNumber);
    default: ASSERT_TRUE(false); return false;
    }
}

bool IocpTransmitStrategy::HandleFailureOperation(const IocpOperation* iocpOperation
    , DWORD byteNumber, int error) {
    switch (iocpOperation->m_type) {
    case IocpOperation::TYPE_RECV:
        return HandleInputFailureOperation(iocpOperation, byteNumber, error);
    case IocpOperation::TYPE_SEND:
        return HandleOutputFailureOperation(iocpOperation, byteNumber, error);
    default: ASSERT_TRUE(false); return false;
    }
}

bool IocpTransmitStrategy::HandleCloseOperation(const IocpOperation* iocpOperation
    , DWORD byteNumber) {
    switch (iocpOperation->m_type) {
    case IocpOperation::TYPE_RECV:
        return HandleInputCloseOperation(iocpOperation, byteNumber);
    case IocpOperation::TYPE_SEND:
        return HandleOutputCloseOperation(iocpOperation, byteNumber);
    default: ASSERT_TRUE(false); return false;
    }
}

bool IocpTransmitStrategy::_PostInputOperation() {
    const int recvLength = m_transmitSocket.GetSocketBuffer().m_recvBuffer.GetPushLength();
    char* recvBuffer = m_transmitSocket.GetSocketBuffer().m_recvBuffer.GetPushBuffer();

    m_recvOperation.m_wsaBuf.buf = recvBuffer;
    m_recvOperation.m_wsaBuf.len = recvLength;

    DWORD bytesRecvd = 0;
    DWORD flags = 0;
    const SockId_t& transmitSockId = m_transmitSocket.GetSockId();
    const int ret = ::WSARecv(transmitSockId
        , &m_recvOperation.m_wsaBuf
        , 1
        , &bytesRecvd
        , &flags
        , &m_recvOperation.m_overlapped
        , NULL);
    if (0 != ret) {
        const DWORD error = ::WSAGetLastError();
        if (ERROR_IO_PENDING != error) {
            HandleFailureOperation(&m_recvOperation, 0, error);
            return false;
        }
    }

    return true;
}

bool IocpTransmitStrategy::_PostOutputOperation() {
    int sendLength = m_transmitSocket.GetSocketBuffer().m_sendBuffer.GetPullLength();
    if (sendLength <= 0) {
        sendLength = m_transmitSocket.GetSocketBuffer().PrepareSendBuffer();
    }
    if (sendLength <= 0) {
        m_sendOperation.SetBusy(false);
        return true;
    }

    char* sendBuffer = m_transmitSocket.GetSocketBuffer().m_sendBuffer.GetPullBuffer();

    m_sendOperation.m_wsaBuf.buf = sendBuffer;
    m_sendOperation.m_wsaBuf.len = sendLength;

    DWORD bytesSent = 0;
    const SockId_t& transmitSockId = m_transmitSocket.GetSockId();
    const int ret = ::WSASend(transmitSockId
        , &m_sendOperation.m_wsaBuf
        , 1
        , &bytesSent
        , 0
        , &m_sendOperation.m_overlapped
        , NULL);
    if (0 != ret) {
        const DWORD error = ::WSAGetLastError();
        if (ERROR_IO_PENDING != error) {
            HandleFailureOperation(&m_sendOperation, 0, error);
            return false;
        }
    }

    return true;
}

bool IocpTransmitStrategy::HandleInputSuccessOperation(const IocpOperation* iocpOperation
    , DWORD byteNumber) {
    ASSERT_TRUE(&m_recvOperation == iocpOperation);

    if (0 == byteNumber) {
        ::printf("Socket Info: socket close. [%s:%d]\n", __FILE__, __LINE__);
        m_transmitSocket.Close();
        return true;
    }

    m_transmitSocket.GetSocketBuffer().m_recvBuffer.JumpPushIndex(byteNumber);

    const char* recvData = m_transmitSocket.GetSocketBuffer().m_recvBuffer.GetPullBuffer();
    const int recvLength = m_transmitSocket.GetSocketBuffer().m_recvBuffer.GetPullLength();

    const int processLength = m_transmitSocket.OnRecvData(recvData, recvLength);
    if (processLength < 0) {
        ::printf("Socket Info: socket close. [%s:%d]\n", __FILE__, __LINE__);
        m_transmitSocket.Close();
        return false;
    }

    m_transmitSocket.GetSocketBuffer().m_recvBuffer.JumpPullIndex(processLength);
    m_transmitSocket.GetSocketBuffer().m_recvBuffer.Compact();

    return _PostInputOperation();
}

bool IocpTransmitStrategy::HandleOutputSuccessOperation(const IocpOperation* iocpOperation
    , DWORD byteNumber) {
    ASSERT_TRUE(&m_sendOperation == iocpOperation);

    if (0 == byteNumber) {
        ::printf("Socket Info: socket close. [%s:%d]\n", __FILE__, __LINE__);
        m_transmitSocket.Close();
        return true;
    }

    const char* sendData = m_transmitSocket.GetSocketBuffer().m_sendBuffer.GetPullBuffer();
    const int sendLength = byteNumber;

    const int processLength = m_transmitSocket.OnSendData(sendData, sendLength);
    if (processLength < 0) {
        ::printf("Socket Info: socket close. [%s:%d]\n", __FILE__, __LINE__);
        m_transmitSocket.Close();
        return false;
    }

    m_transmitSocket.GetSocketBuffer().m_sendBuffer.JumpPullIndex(processLength);
    m_transmitSocket.GetSocketBuffer().m_sendBuffer.Compact();

    return _PostOutputOperation();
}

bool IocpTransmitStrategy::HandleInputFailureOperation(const IocpOperation* iocpOperation
    , DWORD byteNumber, int error) {
    ASSERT_TRUE(&m_recvOperation == iocpOperation);

    ::printf("Socket Error: socket close, error[%d]. [%s:%d]\n", error, __FILE__, __LINE__);
    m_transmitSocket.Close();
    m_transmitSocket.OnError(error);

    m_recvOperation.SetBusy(false);
    return true;
}

bool IocpTransmitStrategy::HandleOutputFailureOperation(const IocpOperation* iocpOperation
    , DWORD byteNumber, int error) {
    ASSERT_TRUE(&m_sendOperation == iocpOperation);

    ::printf("Socket Error: socket close, error[%d]. [%s:%d]\n", error, __FILE__, __LINE__);
    m_transmitSocket.Close();
    m_transmitSocket.OnError(error);

    m_sendOperation.SetBusy(false);
    return true;
}

bool IocpTransmitStrategy::HandleInputCloseOperation(const IocpOperation* iocpOperation
    , DWORD byteNumber) {
    ASSERT_TRUE(&m_recvOperation == iocpOperation);

    ::printf("Socket Info: socket close. [%s:%d]\n", __FILE__, __LINE__);
    m_transmitSocket.Close();

    m_recvOperation.SetBusy(false);
    return true;
}

bool IocpTransmitStrategy::HandleOutputCloseOperation(const IocpOperation* iocpOperation
    , DWORD byteNumber) {
    ASSERT_TRUE(&m_sendOperation == iocpOperation);

    ::printf("Socket Info: socket close. [%s:%d]\n", __FILE__, __LINE__);
    m_transmitSocket.Close();

    m_sendOperation.SetBusy(false);
    return true;
}

DECL_NAMESPACE_MZ_NET_END

#endif // MZ_PLATFORM_WINOWS