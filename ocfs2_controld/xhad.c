#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "o2cb/o2cb.h"

#include "ocfs2_controld.h"

int our_nodeid = 0;
static char *clustername = "xhad";
const char *stackname = "xhad";

int get_clustername(const char **cluster)
{
        if (!cluster)
                return -EINVAL;

        *cluster = clustername;
        return 0;
}

int validate_cluster(const char *cluster)
{
        if (!cluster)
                return 0;

        return !strcmp(cluster, clustername);
}

int setup_stack(void)
{
	return 0;
}
void exit_stack(void)
{
        log_debug("closing xhad connection");
}

int group_join(const char *name,
               void (*set_cgroup)(struct cgroup *cg, void *user_data),
               void (*node_down)(int nodeid, void *user_data),
               void *user_data)
{
        return 0;
}

int group_leave(struct cgroup *cg)
{
	return 0;
}
