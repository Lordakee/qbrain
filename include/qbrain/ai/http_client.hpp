#pragma once
#include <string>
#include <string_view>

namespace qbrain::ai {

struct HttpResponse {
  int status = 0;
  std::string body;
  std::string error;
};

HttpResponse http_post_json(std::string_view base_url, std::string_view path,
                            std::string_view bearer_token, std::string_view json_body,
                            int timeout_ms = 60000);

}  // namespace qbrain::ai
