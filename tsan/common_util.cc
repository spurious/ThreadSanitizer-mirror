#include "common_util.h"

bool StringMatch(const string& wildcard, const string& text) {
  const char* c_text = text.c_str();
  const char* c_wildcard = wildcard.c_str();
  // Start of the current look-ahead. Everything before these positions is a
  // definite, optimal match.
  const char* c_text_last = NULL;
  const char* c_wildcard_last = NULL;

  char last_wc_char = wildcard[wildcard.size() - 1];

  if (last_wc_char == '*' && wildcard.size() == 1) {
    return true;  // '*' matches everything.
  }

  if (last_wc_char != '*' && last_wc_char != '?'
      && last_wc_char != text[text.size() - 1]) {
    // short cut for the case when the wildcard does not end with '*' or '?'
    // and the last characters of wildcard and text do not match.
    return false;
  }

  while (*c_text) {
    if (*c_wildcard == '*') {
      while (*++c_wildcard == '*') {
        // Skip all '*'.
      }
      if (!*c_wildcard) {
        // Ends with a series of '*'.
        return true;
      }
      c_text_last = c_text;
      c_wildcard_last = c_wildcard;
    } else if ((*c_text == *c_wildcard) || (*c_wildcard == '?')) {
      ++c_text;
      ++c_wildcard;
    } else if (c_text_last) {
      // No match. But we have seen at least one '*', so rollback and try at the
      // next position.
      c_wildcard = c_wildcard_last;
      c_text = c_text_last++;
    } else {
      return false;
    }
  }

  // Skip all '*' at the end of the wildcard.
  while (*c_wildcard == '*') {
    ++c_wildcard;
  }

  return !*c_wildcard;
}

string ConvertToPlatformIndependentPath(const string &s) {
  string ret = s;
#ifdef _MSC_VER
  // TODO(timurrrr): do we need anything apart from s/\\///g?
  size_t it = 0;
  while ((it = ret.find("\\", it)) != string::npos) {
    ret.replace(it, 1, "/");
  }
#endif // _MSC_VER
  return ret;
}
