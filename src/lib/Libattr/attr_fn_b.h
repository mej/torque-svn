#include "license_pbs.h" /* See here for the software license */

#include "attribute.h" /* pbs_attribute */
#include "list_link.h" /* tlist_head */
#include "pbs_ifl.h" /* batch_op */

int decode_b(pbs_attribute *patr, char *name, char *rescn, char *val, int perm); 

int encode_b(pbs_attribute *attr, tlist_head *phead, char *atname, char *rsname, int mode, int perm); 

int set_b(pbs_attribute *attr, pbs_attribute *new, enum batch_op op);

int comp_b(pbs_attribute *attr, pbs_attribute *with);


