#include <vector>
#include "../../pfc/pfc.h"

const size_t CRED_BUF_SIZE = 0xff;

struct CredPromptResult : boost::noncopyable {
	CredPromptResult() : 
		un(std::vector<char>(CRED_BUF_SIZE)), 
		pw(std::vector<char>(CRED_BUF_SIZE)) {
	}

	~CredPromptResult() {
		SecureZeroMemory(pw.data(), pw.size());
	}

	std::vector<char> un, pw;
	bool save;
};

std::auto_ptr<CredPromptResult> credPrompt(pfc::string8 msg);
