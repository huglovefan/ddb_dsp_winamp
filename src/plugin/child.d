module ddw.plugin.child;

import core.sys.posix.unistd;
import ddw.plugin.main : Ddw;

enum SUCCESS_LIMIT = 10;
enum FAILURE_LIMIT = 3;

struct Child
{
	pid_t pid = -1;
	int[2] fds = [-1, -1];

	int successes;
	int failures;

	Ddw* pl;
}
