#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>

#define MAX_FILE    (1<<10)
#define MAX_DIR     (1<<10)

enum node_type{
	file, dir
};

struct node{
    char *name;
    char *contents;
	enum node_type type;
	struct node *head;
	struct node *prev;
	struct node *next;
	struct node *parent;
};

struct node *root;
struct node *now;

static struct node *node_init(char *name, char *contents, enum node_type type){
	struct node *cur = (struct node *)malloc(sizeof(struct node));
	cur->name = name;
	cur->contents = contents;
	cur->type = type;
	cur->prev = NULL;
	cur->next = NULL;
	if (type == dir){
		cur->head = (struct node *)malloc(sizeof(struct node));
		cur->head->prev = NULL;
		cur->head->next = NULL;
	}
	else cur->head = NULL;
	cur->parent = NULL;
	return cur;
}


static void insert(struct node *prev, struct node *next, struct node *new){
	new->prev = prev;
	new->next = next;
	if (prev) prev->next = new;
	if (next) next->prev = new;
}

static void remove_(struct node *del){
	struct node *next = del->next;
	struct node *prev = del->prev;
	free(del);
	if (prev) prev->next = next;
	if (next) next->prev = prev; 
}

static int insert_in_dir(struct node *cur, struct node *new){
	new->parent = cur;
	struct node *next = cur->head->next;
	struct node *prev = cur->head;
	while(next){
		if (strcmp(next->name, new->name) > 0) break;
		if (strcmp(next->name, new->name) == 0) return -EEXIST;
		prev = next;
		next = next->next;
	}
	insert(prev, next, new);
	return 0;
}

static int remove_in_dir(struct node *cur, struct node *del){
	cur = cur->head->next;
	while(cur){
		if (strcmp(cur->name, del->name) > 0) return -ENOENT;
		if (strcmp(cur->name, del->name) == 0){
			remove_(cur);
			return 0;
		}
		cur = cur->next;
	}
	return -ENOENT;
}

static struct node *name_node(struct node *cur, const char *name){
	if (cur->type == file) return NULL;
	cur = cur->head->next;
	while(cur){
		if (strcmp(cur->name, name) == 0)
			return cur;
		cur = cur->next;
	}
	return NULL;
}

static struct node *get_node(const char *path){
	struct node *cur = root;
	int len = strlen(path);
	for (int l = 1, r; l < len; l = r + 1){
		r = l;
		while(r < len && path[r] != '/') r++;
		char *name = (char *)malloc(sizeof(char) * (r - l + 1));
		for (int i = 0; i < r - l; i++)
			name[i] = path[i + l];
		name[r - l] = '\0';
		cur = name_node(cur, name);
		free(name);
		if (!cur) return NULL;
	}
	return cur;
}

static void *chat_init(struct fuse_conn_info *conn, struct fuse_config *cfg){
	root = node_init("root", "", dir);
	now = root;
	return NULL;
}

static int chat_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi){
	(void) fi;
	int res = 0;
	memset(stbuf, 0, sizeof(struct stat));

	struct node *cur = get_node(path);

	if (!cur) res = -ENOENT;
	else if (cur->type == dir){
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	}
	else{
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(cur->contents);
	}
	return res;
}

static int chat_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 			off_t offset, struct fuse_file_info *fi,
			 			enum fuse_readdir_flags flags){
	(void) offset;
	(void) fi;
	(void) flags;

	struct node *cur = get_node(path);
	if (!cur) return -ENOENT;
	else if (cur->type == file) return -ENOTDIR;

	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	cur = cur->head->next;
	while(cur){
		filler(buf, cur->name, NULL, 0, 0);
		cur = cur->next;
	}

	return 0;
}

static int chat_mkdir(const char *path, mode_t mode){
	int len = strlen(path);
	char *old = (char *)malloc(sizeof(char) * (len + 1));
	char *name = (char *)malloc(sizeof(char) * (len + 1));
	int lst = -1;
	for (int i = len - 2; i >= 0; i--){
		if (path[i] == '/'){
			lst = i;
			break;
		}
	}
	if (lst == -1) return -EPERM;
	lst++;
	for (int i = lst; i < len; i++) name[i - lst] = path[i];
	name[len - lst] = '\0';
	if (path[len - 1] == '/') name[len - 1 - lst] = '\0';
	for (int i = 0; i < lst; i++) old[i] = path[i];
	old[lst] = '\0';
	struct node *cur = get_node(old);
	free(old);
	if (cur != root) return -EPERM;
	
	struct node *new = node_init(name, "", dir);
	return insert_in_dir(cur, new);
}

static int chat_read(const char *path, char *buf, size_t size, off_t offset,
		      		 struct fuse_file_info *fi){
	size_t len;
	(void) fi;

	struct node *cur = get_node(path);
	if (!cur) return -ENOENT;
	else if (cur->type == dir) return -EPERM;

	len = strlen(cur->contents);
	if (offset < len) {
		if (offset + size > len) size = len - offset;
		memcpy(buf, cur->contents + offset, size);
	}
	else size = 0;

	return size;
}

static int chat_write(const char *path, const char *content, size_t size,
					  off_t offset, struct fuse_file_info* fi){
	(void) fi;

	int len = strlen(path);

	char *from_name = (char *)malloc(sizeof(char) * (len + 1));
	char *from_dir = (char *)malloc(sizeof(char) * (len + 1));
	char *to_name = (char *)malloc(sizeof(char) * (len + 1));
	char *to_dir = (char *)malloc(sizeof(char) * (len + 1));

	int lst = -1;
	for (int i = len - 2; i >= 0; i--){
		if (path[i] == '/'){
			lst = i;
			break;
		}
	}
	if (lst == -1) return -EPERM;
	lst++;
	for (int i = lst; i < len; i++) to_name[i - lst] = path[i];
	to_name[len - lst] = '\0';
	if (path[len - 1] == '/') to_name[len - 1 - lst] = '\0';

	len = strlen(to_name);
	to_dir[0] = '/';
	for (int i = 0; i < len; i++) to_dir[i + 1] = to_name[i];
	to_dir[len + 1] = '\0';

	lst--;
	for (int i = 0; i < lst; i++) from_dir[i] = path[i];
	from_dir[lst] = '\0';
	len = lst;
	lst = -1;
	for (int i = len - 2; i >= 0; i--){
		if (path[i] == '/'){
			lst = i;
			break;
		}
	}
	if (lst != 0) return -EPERM;
	for (int i = 0; i < len - 1; i++) from_name[i] = from_dir[i + 1];
	from_name[len - 1] = '\0';

	struct node *to_dir_node = get_node(to_dir);
	if (!to_dir_node) return -ENOENT;
	else if (to_dir_node->type == file) return -EPERM;

	struct node *from_node = name_node(to_dir_node, from_name);
	if (!from_node){
		from_node = node_init(from_name, "", file);
		insert_in_dir(to_dir_node, from_node);
	}

	char *from_content = from_node->contents;
	offset = strlen(from_content);
	from_node->contents = (char *)malloc(sizeof(char) * (size + offset + 1));
	memcpy(from_node->contents, from_content, offset);
	memcpy(from_node->contents + offset, content, size);

	return size;
}

static int chat_unlink(const char *path){
	struct node *cur = get_node(path);
	if (!cur) return -ENOENT;
	else if (cur->type == dir) return -EPERM;
	return remove_in_dir(cur->parent, cur);
}

static int chat_create(const char *path, mode_t mode, struct fuse_file_info *fi){
	(void) fi;

	int len = strlen(path);
	char *old = (char *)malloc(sizeof(char) * (len + 1));
	char *name = (char *)malloc(sizeof(char) * (len + 1));
	int lst = -1;
	for (int i = len - 2; i >= 0; i--){
		if (path[i] == '/'){
			lst = i;
			break;
		}
	}
	if (lst == -1) return -EPERM;
	lst++;
	for (int i = lst; i < len; i++) name[i - lst] = path[i];
	name[len - lst] = '\0';
	if (path[len - 1] == '/') name[len - 1 - lst] = '\0';
	for (int i = 0; i < lst; i++) old[i] = path[i];
	old[lst] = '\0';
	struct node *cur = get_node(old);
	free(old);
	if (!cur) return -ENOENT;
	else if (cur->type == file) return -ENOTDIR;
	
	struct node *new = node_init(name, "", file);
	return insert_in_dir(cur, new);
}

static int chat_rmdir(const char *path){
	struct node *cur = get_node(path);
	if (!cur) return -ENOENT;
	else if (cur->type == file) return -ENOTDIR;
	return remove_in_dir(cur->parent, cur);
}

static int chat_utimens (const char *path, const struct timespec tv[2],
						 struct fuse_file_info *fi){
	return 0;
}

static const struct fuse_operations chat_oper = {
	.init       = chat_init,
	.getattr	= chat_getattr,
	.readdir	= chat_readdir,
	.mkdir		= chat_mkdir,
	.read		= chat_read,
	.write		= chat_write,
	.unlink		= chat_unlink,
	.create		= chat_create,
	.rmdir		= chat_rmdir,
	.utimens	= chat_utimens
};


int main(int argc, char *argv[]){
	return fuse_main(argc, argv, &chat_oper, NULL);
}
