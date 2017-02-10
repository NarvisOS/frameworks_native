#ifndef ANDROID_PDX_UDS_IPC_HELPER_H_
#define ANDROID_PDX_UDS_IPC_HELPER_H_

#include <sys/socket.h>
#include <utility>
#include <vector>

#include <pdx/rpc/serializable.h>
#include <pdx/rpc/serialization.h>
#include <pdx/status.h>
#include <pdx/utility.h>

namespace android {
namespace pdx {
namespace uds {

#define RETRY_EINTR(fnc_call)                 \
  ([&]() -> decltype(fnc_call) {              \
    decltype(fnc_call) result;                \
    do {                                      \
      result = (fnc_call);                    \
    } while (result == -1 && errno == EINTR); \
    return result;                            \
  })()

class SendPayload : public MessageWriter, public OutputResourceMapper {
 public:
  Status<void> Send(int socket_fd);
  Status<void> Send(int socket_fd, const ucred* cred);

  // MessageWriter
  void* GetNextWriteBufferSection(size_t size) override;
  OutputResourceMapper* GetOutputResourceMapper() override;

  // OutputResourceMapper
  FileReference PushFileHandle(const LocalHandle& handle) override;
  FileReference PushFileHandle(const BorrowedHandle& handle) override;
  FileReference PushFileHandle(const RemoteHandle& handle) override;
  ChannelReference PushChannelHandle(const LocalChannelHandle& handle) override;
  ChannelReference PushChannelHandle(
      const BorrowedChannelHandle& handle) override;
  ChannelReference PushChannelHandle(
      const RemoteChannelHandle& handle) override;

 private:
  ByteBuffer buffer_;
  std::vector<int> file_handles_;
};

class ReceivePayload : public MessageReader, public InputResourceMapper {
 public:
  Status<void> Receive(int socket_fd);
  Status<void> Receive(int socket_fd, ucred* cred);

  // MessageReader
  BufferSection GetNextReadBufferSection() override;
  void ConsumeReadBufferSectionData(const void* new_start) override;
  InputResourceMapper* GetInputResourceMapper() override;

  // InputResourceMapper
  bool GetFileHandle(FileReference ref, LocalHandle* handle) override;
  bool GetChannelHandle(ChannelReference ref,
                        LocalChannelHandle* handle) override;

 private:
  ByteBuffer buffer_;
  std::vector<LocalHandle> file_handles_;
  size_t read_pos_{0};
};

template <typename FileHandleType>
class ChannelInfo {
 public:
  FileHandleType data_fd;
  FileHandleType event_fd;

 private:
  PDX_SERIALIZABLE_MEMBERS(ChannelInfo, data_fd, event_fd);
};

template <typename FileHandleType>
class RequestHeader {
 public:
  int32_t op{0};
  ucred cred;
  uint32_t send_len{0};
  uint32_t max_recv_len{0};
  std::vector<FileHandleType> file_descriptors;
  std::vector<ChannelInfo<FileHandleType>> channels;
  std::array<uint8_t, 32> impulse_payload;
  bool is_impulse{false};

 private:
  PDX_SERIALIZABLE_MEMBERS(RequestHeader, op, send_len, max_recv_len,
                           file_descriptors, channels, impulse_payload,
                           is_impulse);
};

template <typename FileHandleType>
class ResponseHeader {
 public:
  int32_t ret_code{0};
  uint32_t recv_len{0};
  std::vector<FileHandleType> file_descriptors;
  std::vector<ChannelInfo<FileHandleType>> channels;

 private:
  PDX_SERIALIZABLE_MEMBERS(ResponseHeader, ret_code, recv_len, file_descriptors,
                           channels);
};

template <typename T>
inline Status<void> SendData(int socket_fd, const T& data) {
  SendPayload payload;
  rpc::Serialize(data, &payload);
  return payload.Send(socket_fd);
}

template <typename FileHandleType>
inline Status<void> SendData(int socket_fd,
                             const RequestHeader<FileHandleType>& request) {
  SendPayload payload;
  rpc::Serialize(request, &payload);
  return payload.Send(socket_fd, &request.cred);
}

Status<void> SendData(int socket_fd, const void* data, size_t size);
Status<void> SendDataVector(int socket_fd, const iovec* data, size_t count);

template <typename T>
inline Status<void> ReceiveData(int socket_fd, T* data) {
  ReceivePayload payload;
  Status<void> status = payload.Receive(socket_fd);
  if (status && rpc::Deserialize(data, &payload) != rpc::ErrorCode::NO_ERROR)
    status.SetError(EIO);
  return status;
}

template <typename FileHandleType>
inline Status<void> ReceiveData(int socket_fd,
                                RequestHeader<FileHandleType>* request) {
  ReceivePayload payload;
  Status<void> status = payload.Receive(socket_fd, &request->cred);
  if (status && rpc::Deserialize(request, &payload) != rpc::ErrorCode::NO_ERROR)
    status.SetError(EIO);
  return status;
}

Status<void> ReceiveData(int socket_fd, void* data, size_t size);
Status<void> ReceiveDataVector(int socket_fd, const iovec* data, size_t count);

size_t CountVectorSize(const iovec* data, size_t count);
void InitRequest(android::pdx::uds::RequestHeader<BorrowedHandle>* request,
                 int opcode, uint32_t send_len, uint32_t max_recv_len,
                 bool is_impulse);

Status<void> WaitForEndpoint(const std::string& endpoint_path,
                             int64_t timeout_ms);

}  // namespace uds
}  // namespace pdx
}  // namespace android

#endif  // ANDROID_PDX_UDS_IPC_HELPER_H_