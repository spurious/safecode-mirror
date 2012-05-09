#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>

#include "safecode/Runtime/BBMetaData.h"

int next_pow_of_2(size_t size) {
  unsigned int i;
  for (i = 1; i < size; i = i << 1);
  return (i < 16 ? 16 : i);
}

extern "C" void* malloc(size_t size) {
  size_t adjusted_size = size + sizeof(BBMetaData);
  size_t aligned_size = next_pow_of_2(adjusted_size);
  void *vp = memalign(aligned_size, aligned_size);

  BBMetaData *data = (BBMetaData*)((uintptr_t)vp + aligned_size - sizeof(BBMetaData));
  data->size = size;
  data->pool = NULL;
  return vp;
}

extern "C" void* calloc(size_t nmemb, size_t size) {
  size_t aligned_size = next_pow_of_2(nmemb*size+sizeof(BBMetaData));
  void *vp = memalign(aligned_size, aligned_size);
  memset(vp, 0, aligned_size);
  BBMetaData *data = (BBMetaData*)((uintptr_t)vp + aligned_size - sizeof(BBMetaData));
  data->size = nmemb*size;
  data->pool = NULL;
  return vp;
}

extern "C" void* realloc(void *ptr, size_t size) {
  if (ptr == NULL) {
    return malloc(size);
  }

  size += sizeof(BBMetaData);
  size_t aligned_size = next_pow_of_2(size);
  void *vp = memalign(aligned_size, aligned_size);
  memcpy(vp, ptr, size);
  free(ptr);
  BBMetaData *data = (BBMetaData*)((uintptr_t)vp + aligned_size - sizeof(BBMetaData));
  data->size = size;
  data->pool = NULL;
  return vp;
}
