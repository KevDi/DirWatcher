// DirectoryWatcher.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <chrono>
#include <functional>
#include <vector>
#include <filesystem>
#include <thread>
#include <windows.h>

class DirectoryWatcher {
public:
  using Callback = std::function<void(const std::string&)>;
  DirectoryWatcher(const std::filesystem::path& input_directory, std::vector<std::string> file_types, Callback callback);

  explicit operator bool() const {
    return is_valid_;
  }

  bool watch();

  void stop();

  size_t processedFilesCount() const { return processed_files_count; }
  size_t ignoredFilesCount() const { return ignored_files_count; }

private:

  bool eventRecv();
  bool eventSend();
  void handleEvents();
  bool hasEvent() const {
    return event_buf_len_ready_ != 0;
  }
  bool isProcessableFile(const std::filesystem::path& path) const;
  bool isValidAction(int action) const;

  std::filesystem::path input_directory_;
  std::vector<std::string> file_types_{};
  Callback callback_;
  HANDLE path_handle_;
  HANDLE completion_token_{ INVALID_HANDLE_VALUE };
  unsigned long event_buf_len_ready_{ 0 };
  bool is_valid_{ false };
  OVERLAPPED event_overlap_{};
  std::vector<std::byte> event_buf_{ 64 * 1024 };
  size_t processed_files_count{ 0 };
  size_t ignored_files_count{ 0 };

};


DirectoryWatcher::DirectoryWatcher(const std::filesystem::path& input_directory, std::vector<std::string> file_types, Callback callback)
  : input_directory_{ input_directory },
  file_types_{ file_types },
  callback_{ std::move(callback) } {

  path_handle_ = CreateFileA(input_directory.string().c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);

  if (path_handle_ != INVALID_HANDLE_VALUE) {
    completion_token_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
  }

  if (completion_token_ != INVALID_HANDLE_VALUE) {
    is_valid_ = CreateIoCompletionPort(path_handle_, completion_token_, (ULONG_PTR)path_handle_, 1);
  }
}

bool DirectoryWatcher::watch() {
  if (is_valid_) {
    eventRecv();

    while (is_valid_ && hasEvent()) {
      eventSend();
    }

    while (is_valid_) {
      ULONG_PTR completion_key{ 0 };
      LPOVERLAPPED overlap{ nullptr };

      bool complete = GetQueuedCompletionStatus(completion_token_, &event_buf_len_ready_, &completion_key, &overlap, 16);

      if(complete && event_buf_len_ready_ == 0) {
        std::cerr << "Scan dir manually\n'";
        eventRecv();
      }

      if (complete && overlap) {
        handleEvents();
      }
    }
    return true;
  } else {
    return false;
  }


}

void DirectoryWatcher::handleEvents() {
  while (is_valid_ && hasEvent()) {
    eventSend();
    eventRecv();
  }
}

void DirectoryWatcher::stop() {
  is_valid_ = false;
}

bool DirectoryWatcher::eventRecv() {
  event_buf_len_ready_ = 0;
  DWORD bytes_returned = 0;
  memset(&event_overlap_, 0, sizeof(OVERLAPPED));

  auto read_ok = ReadDirectoryChangesW(path_handle_, event_buf_.data(), event_buf_.size(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME, &bytes_returned, &event_overlap_, nullptr);

  if (!event_buf_.empty() && read_ok) {
    event_buf_len_ready_ = bytes_returned > 0 ? bytes_returned : 0;
    return true;
  }
  if (GetLastError() == ERROR_IO_PENDING) {
    event_buf_len_ready_ = 0;
    is_valid_ = false;
  }
  return false;
}

bool DirectoryWatcher::eventSend() {

  auto buf = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(event_buf_.data());

  if (is_valid_) {
    while (buf + sizeof(FILE_NOTIFY_INFORMATION) <= buf + event_buf_len_ready_) {
      auto filename = input_directory_ / std::wstring{ buf->FileName, buf->FileNameLength / 2 };
      if (isValidAction(buf->Action) && isProcessableFile(filename)) {
        callback_({ filename.string() });
        processed_files_count++;
      } else {
        ignored_files_count++;
      }
      if (buf->NextEntryOffset == 0) {
        break;
      }

      buf = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<std::byte*>(buf) + buf->NextEntryOffset);
    }
    return true;
  } else {
    return false;
  }
}

bool DirectoryWatcher::isProcessableFile(const std::filesystem::path& path) const {
  // Get extension and erase first character (dot-character of extension)
  std::string extension = path.extension().string().erase(0, 1);
  std::transform(extension.begin(), extension.end(), extension.begin(),
    [](unsigned char c) { return std::tolower(c); });
  return std::find(file_types_.begin(), file_types_.end(), extension) != file_types_.end();
}

bool DirectoryWatcher::isValidAction(int action) const {
  return action == FILE_ACTION_ADDED || action == FILE_ACTION_RENAMED_NEW_NAME;
}

int main() {
  int counter{ 0 };
  std::cout << "Hello World!\n";
  DirectoryWatcher watcher{ "C:\\temp\\dummy", {"dat"}, [&counter](const std::string& filename) {
    counter++;
    std::cout << "counter: " << counter << '\n';
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    //processing
    try {
      std::filesystem::remove(filename);
    } catch(const std::exception& exc) {
      std::cerr << exc.what();
    }
  } };

  std::thread th1{ &DirectoryWatcher::watch, &watcher };

  std::this_thread::sleep_for(std::chrono::seconds(300));

  watcher.stop();

  th1.join();
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
