#include <vector>
#include "../../pfc/pfc.h"

const size_t CRED_BUF_SIZE = 0xff;

struct CredPromptResult {
	CredPromptResult() : 
		un(std::vector<char>(CRED_BUF_SIZE)), 
		pw(std::vector<char>(CRED_BUF_SIZE)) {
	}

	std::vector<char> un, pw;
	bool save;
};

CredPromptResult credPrompt(pfc::string8 msg);
