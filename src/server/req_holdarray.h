#include "license_pbs.h" /* See here for the software license */
#ifndef _REQ_HOLDARRAY_H
#define _REQ_HOLDARRAY_H

#include "attribute.h" /* attribute */

void hold_job(attribute *temphold, void *j);

int req_holdarray(struct batch_request *preq);

#endif /* _REQ_HOLDARRAY_H */
