#include "cachelab.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const unsigned m = 64;

typedef struct {
	unsigned char valid;
	unsigned int tag;
	unsigned used_seq;
} line;

typedef struct {
	line *lines;
	unsigned max_used_seq;
} set;

int main(int argc, char **argv) {
	int s;
	int E;
	int b;
	FILE *f;
	for(int i = 0; i < argc; i++) {
		if(strcmp(argv[i], "-s") == 0) {
			s = atoi(argv[i + 1]);
		} else if(strcmp(argv[i], "-E") == 0) {
			E = atoi(argv[i + 1]);
		} else if(strcmp(argv[i], "-b") == 0) {
			b = atoi(argv[i + 1]);
		} else if(strcmp(argv[i], "-t") == 0) {
			f = fopen(argv[i + 1], "r");
		}
	}
	if(f == NULL) {
		fprintf(stderr, "Could not open trace file\n");
		return -1;
	}
	unsigned S = 1 << s;///< number if sets
	//unsigned B = 1 << b;                                 ///< number of blocks
	unsigned t = m - (s + b);///< tag size
	//printf("s = %d, E = %d, S = %d\n", s, E, S);//test
	set *sets = malloc(sizeof(set) * S);
	for(unsigned i = 0; i < S; i++) {
		sets[i].lines        = malloc(sizeof(line) * E);
		sets[i].max_used_seq = 0;
		for(unsigned j = 0; j < E; j++) {
			sets[i].lines[j].valid    = 0;
			sets[i].lines[j].tag      = 0;
			sets[i].lines[j].used_seq = 0;
		}
	}
	int hits      = 0;
	int misses    = 0;
	int evictions = 0;
	char operation;
	unsigned address, size;
	while(!feof(f)) {
		fscanf(f, " %c %x,%d ", &operation, &address, &size);
		unsigned set_index = (address >> b) & ~(-1 << s);
		unsigned tag       = (address >> (s + b)) & ~(-1 << t);
		//printf("%c %x,%d\n", operation, address, size);
		//printf("hits: %d, misses %d, evictions %d\n", hits, misses, evictions);
		//printf("set_index\t%d\ntag\t%d\n", set_index, tag);
		switch(operation) {
			case 'I':
				break;
			case 'L':
			case 'S':
			case 'M':
				unsigned hit = 0;
				for(unsigned i = 0; i < E; i++) {
					if(sets[set_index].lines[i].valid && sets[set_index].lines[i].tag == tag) {
						hit = 1;
						hits++;
						sets[set_index].max_used_seq++;
						sets[set_index].lines[i].used_seq = sets[set_index].max_used_seq;
						break;
					}
				}
				if(!hit) {
					unsigned min_used_seq   = UINT_MAX;
					unsigned eviction_index = 0;
					unsigned loaded         = 0;
					misses++;
					for(unsigned i = 0; i < E; i++) {
						if(!sets[set_index].lines[i].valid) {
							loaded                         = 1;
							sets[set_index].lines[i].valid = 1;
							sets[set_index].lines[i].tag   = tag;
							sets[set_index].max_used_seq++;
							sets[set_index].lines[i].used_seq = sets[set_index].max_used_seq;
							break;
						} else if(sets[set_index].lines[i].used_seq < min_used_seq) {
							min_used_seq   = sets[set_index].lines[i].used_seq;
							eviction_index = i;
						}
					}
					if(!loaded) {
						evictions++;
						sets[set_index].lines[eviction_index].valid = 1;
						sets[set_index].lines[eviction_index].tag   = tag;
						sets[set_index].max_used_seq++;
						sets[set_index].lines[eviction_index].used_seq = sets[set_index].max_used_seq;
					}
				}
				if(operation == 'M') {
					hits++;
				}
				break;
			default:
				fprintf(stderr, "Unknown operation: %c\n", operation);
				return -1;
		}
	}
	fclose(f);
	printSummary(hits, misses, evictions);
	return 0;
}
