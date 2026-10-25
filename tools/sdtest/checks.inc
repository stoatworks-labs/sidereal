// The checks: included into main.cpp's anonymous namespace.

struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {};
	return list;
}

int runNegative( bool offlineOnly = false )
{
	(void)offlineOnly;
	return 0;
}

int runOffline()
{
	return 0;
}

int runBench()
{
	return 0;
}
