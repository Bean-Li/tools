#ifndef _DLIST_H_
#define _DLIST_H_

typedef struct list_head{
    struct list_head* next ; 
    struct list_head* prev ; 
} list_head;

void INIT_LIST_HEAD(struct list_head* list)
{
    list->next = list ;
    list->prev = list ;
}

void __list_add(struct list_head* new,
                       struct list_head* prev,
		       struct list_head* next)
{
    next->prev = new ; 
    new->next = next ;
    new->prev = prev ; 
    prev->next = new ;
}

  void list_add(struct list_head *new, struct list_head *head)
{
      __list_add(new, head, head->next);
}

void list_add_tail(struct list_head *new, struct list_head *head)
{
      __list_add(new, head->prev, head);
}


void __list_del(struct list_head * prev, struct list_head * next)
{
      next->prev = prev;
      prev->next = next;
}

void list_del(struct list_head* entry)
{
    __list_del(entry->prev, entry->next);
    entry->prev = NULL ;
    entry->next = NULL ;
}

#define offsetof(TYPE, MEMBER)  ((size_t)&((TYPE *)0)->MEMBER)
#define container_of(ptr, type, member) ({                      \
	const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
	(type *)( (char *)__mptr - offsetof(type,member) );}) 

#define list_entry(ptr, type, member) \
      container_of(ptr, type, member)


#endif
