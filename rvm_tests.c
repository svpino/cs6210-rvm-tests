#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include "rvm.h"

static int test_count = 0;
static int failed_test_count = 0;

static void assertTrue(char * test, char * message, bool condition);
static char * get_segment_file_name(char * directory, char * segname);
static char * get_log_file_name(char * directory);
static void copy_test_template(char * source, char * target, char * directory);

int main(int argc, char **argv) {
	char * directory = "RVM";
	char * log_filename = get_log_file_name(directory);
 	

	/* 
	 * TEST 1 - rvm_init
	 */

	rvm_t rvm = rvm_init(NULL);
	assertTrue(" 1a", "Initialization method should fail if directory is NULL", rvm == NULL);

	rvm = rvm_init("ThisIsADirectoryNameWithMoreThan128CharactersSoICanTestWhetherMyInitializationMethodChecksForThisSizeAndPreventsThisDirectoryToBeCreated");
	assertTrue(" 1b", "Initialization method should fail if directory size is longer than 128 characters", rvm == NULL);

 	rvm = rvm_init(directory);
	assertTrue(" 1c", "Directory was created", rvm != NULL);
	assertTrue(" 1d", "Returned directory name matches the name provided", strcmp(rvm->prefix, directory) == 0);
	assertTrue(" 1e", "Segment list is properly initialized", rvm->segst.first == NULL && rvm->segst.N == 0);

	/* 
	 * TEST 2 - Makes sure that the specified comparison function for the segment list works properly.
	 */

	seqsrchst_put(&rvm->segst, "test-key1", "test-value1");
	assertTrue(" 2a", "Segment should be on the list", seqsrchst_contains(&rvm->segst, "test-key1"));
	assertTrue(" 2b", "Segment shouldn't be on the list", !seqsrchst_contains(&rvm->segst, "test-key2"));
	seqsrchst_delete(&rvm->segst, "test-key1");

	/*
	 * TEST 3 - rvm_map
	 */

	char * segment = (char *) rvm_map(NULL, "segment1", 10000);
	assertTrue(" 3a", "Mapping should fail is rvm is not specified", segment == NULL);

	rvm_t rvm2 = malloc(sizeof(*rvm));
	segment = (char *) rvm_map(rvm2, "segment1", 10000);
	assertTrue(" 3b", "Mapping should fails if supplied rvm doesn't specify a directory", segment == NULL);
	free(rvm2);

	segment = (char *) rvm_map(rvm, "ThisIsASegmentNameWithMoreThan128CharactersSoICanTestWhetherMyMappingMethodChecksForThisSizeAndPreventsThisSegmentToBeMappedInMyLibrary", 10000);
	assertTrue(" 3c", "Mapping should fail if segment name is longer than 128 characters", segment == NULL);

	segment = (char *) rvm_map(rvm, "segment1", 10000);
	
	segment_t st = seqsrchst_get(&rvm->segst, "segment1");

	assertTrue(" 3d", "segment.size is initialized", st->size == 10000);	
	assertTrue(" 3e", "segment.segname is initialized", strcmp(st->segname, "segment1") == 0);	
	assertTrue(" 3f", "segment.segbase is initialized", st->segbase == segment);	

	seqsrchst_delete(&rvm->segst, "segment1");

	copy_test_template("rvm1.log", "rvm.log", directory);

	char * segment1 = (char *) rvm_map(rvm, "segment1", 14);
	char * segment2 = (char *) rvm_map(rvm, "segment2", 14);

	assertTrue(" 3g", "Segments should be initialized from log file during mapping", strcmp(segment1, "segment1-value") == 0 && strcmp(segment2, "segment2-value") == 0);

	seqsrchst_delete(&rvm->segst, "segment1");
	seqsrchst_delete(&rvm->segst, "segment2");
	remove(log_filename);

	copy_test_template("segment1", "segment1", directory);
	segment = (char *) rvm_map(rvm, "segment1", 12);
	assertTrue(" 3h", "Segments should be initialized from segment file during mapping", strcmp(segment, "hello world!") == 0);

	segment = (char *) rvm_map(rvm, "segment1", 10000);

	assertTrue(" 3i", "Segment is mapped into the list of segments", seqsrchst_contains(&rvm->segst, "segment1"));
	assertTrue(" 3j", "Function rvm_map returns pointer to segment", segment != NULL);
	assertTrue(" 3k", "List of segments doesn't have an inexistent segment", !seqsrchst_contains(&rvm->segst, "segment-invalid"));

	segment2 = (char *) rvm_map(rvm, "segment1", 10000);
	assertTrue(" 3l", "Trying to map same segment twice returns existing segment", segment == segment2);
	assertTrue(" 3m", "Trying to map same segment twice doesn't add new segment to list", seqsrchst_size(&rvm->segst) == 1);

	char * segment3 = (char *) rvm_map(rvm, "segment1", 5000);
	assertTrue(" 3n", "If specified size is shorter, segment shouldn't be mapped again", segment == segment3);
	assertTrue(" 3o", "If specified size is shorter, segment shouldn't be added again to the list", seqsrchst_size(&rvm->segst) == 1);

	char * segment4 = (char *) rvm_map(rvm, "segment1", 20000);
	assertTrue(" 3p", "If specified size is larger, returned segment should be different", segment != segment4);

	assertTrue(" 3q", "If specified size is larger, segment shouldn't be added again to the list", seqsrchst_size(&rvm->segst) == 1);

	st = seqsrchst_get(&rvm->segst, "segment1");
	assertTrue(" 3r", "If specified size is larger, segment should be resized", st->size == 20000);

	st = seqsrchst_get(&rvm->segst, "segment1");
	st->cur_trans = segment;
	segment = (char *) rvm_map(rvm, "segment1", 20000);
	assertTrue(" 3s", "If segment is involved in a transaction, return NULL", segment == NULL);

	seqsrchst_delete(&rvm->segst, "segment1");

	/*
	 * TEST 4 - Tests all the logic about creating the segment files.
	 */

	char * segment_file_name = get_segment_file_name(directory, "segment1");

	remove(segment_file_name); 

	struct stat file_stat;

	segment = (char *) rvm_map(rvm, "segment1", 10000);

	assertTrue(" 4a", "If segment doesn't exist and file doesn't exist either, we need to create a new file", stat(segment_file_name, &file_stat) == 0);
	assertTrue(" 4b", "Created segment file should have same size of the segment", file_stat.st_size == 10000);

	seqsrchst_delete(&rvm->segst, "segment1");
	remove(segment_file_name); 
	
	FILE * file = fopen(segment_file_name, "w");
	fwrite("Hello World!", 12, 1, file);
	fclose(file);

	segment = (char *) rvm_map(rvm, "segment1", 10000);
	stat(segment_file_name, &file_stat);
	assertTrue(" 4c", "If segment doesn't exist and file exists, we need to load file in memory", strcmp(segment, "Hello World!") == 0);

	segment = (char *) rvm_map(rvm, "segment1", 20000);
	stat(segment_file_name, &file_stat);
	assertTrue(" 4d", "If segment exists and file exists, and size is larger, we need to resize the existing file", file_stat.st_size == 20000);

	remove(segment_file_name);
	segment = (char *) rvm_map(rvm, "segment1", 30000);
	stat(segment_file_name, &file_stat);
	assertTrue(" 4e", "If segment exists and file doesn't exist, and size is larger, we need to create the file", file_stat.st_size == 30000);
	
	segment2 = (char *) rvm_map(rvm, "segment1", 30000);
	stat(segment_file_name, &file_stat);
	assertTrue(" 4f", "If segment exists and file exists, we don't have to do anything", segment == segment2 && file_stat.st_size == 30000);

	remove(segment_file_name);
	stat(segment_file_name, &file_stat);
	assertTrue(" 4g", "If segment exists and file doesn't exist, we need to create the file", file_stat.st_size == 30000);

	seqsrchst_delete(&rvm->segst, "segment1");

	/*
	 * TEST 5 - rvm_unmap
	 */

	segment = (char *) rvm_map(rvm, "segment1", 10000);
	rvm_unmap(NULL, segment);
	assertTrue(" 5a", "Segment shouldn't be unmapped if rvm is not initialized", seqsrchst_size(&rvm->segst) == 1);		

	segment = (char *) rvm_map(rvm, "segment1", 10000);
	rvm_unmap(rvm, segment);
	assertTrue(" 5b", "Segment should be removed from list when unmapped", seqsrchst_size(&rvm->segst) == 0);

	void * segbase = malloc(1);
	rvm_unmap(rvm, segbase);
	assertTrue(" 5c", "Nothing should happen if invalid segment is unmapped", seqsrchst_size(&rvm->segst) == 0);	
	free(segbase);

	segment = (char *) rvm_map(rvm, "segment1", 10000);
	st = seqsrchst_get(&rvm->segst, "segment1");
	st->cur_trans = segment;
	rvm_unmap(rvm, segment);
	assertTrue(" 5d", "Nothing should happen if specified segment is related to an active transaction", seqsrchst_size(&rvm->segst) == 1);	

	st->cur_trans = NULL;
	copy_test_template("rvm1.log", "rvm.log", directory);
	rvm_unmap(rvm, segment);
	assertTrue(" 5e", "Segment memory should be updated from log file", strcmp(segment, "segment1-value") == 0);	

	stat(log_filename, &file_stat);
	assertTrue(" 5f", "Log file should be truncated", file_stat.st_size == 41);	

	/*
	 * TEST 6 - rvm_destroy
	 */

	segment = (char *) rvm_map(rvm, "segment1", 10000);
	rvm_destroy(NULL, "segment1");
	assertTrue(" 6a", "Segment shouldn't be destroyed if rvm is not initialized", stat(segment_file_name, &file_stat) == 0);		

	segment = (char *) rvm_map(rvm, "segment1", 10000);
	rvm_destroy(rvm, "segment1");
	assertTrue(" 6b", "Segment file shouldn't be destroyed if segment is mapped", stat(segment_file_name, &file_stat) == 0);		

	rvm_unmap(rvm, segment);
	rvm_destroy(rvm, "segment1");
	assertTrue(" 6c", "Segment file should be destroyed if segment is not mapped", stat(segment_file_name, &file_stat) != 0);		

	/*
	 * TEST 7 - rvm_begin_trans
	 */

	segment = (char *) rvm_map(rvm, "segment1", 10000);
	trans_t trans = rvm_begin_trans(NULL, 1, segment);
	assertTrue(" 7a", "Transaction should return -1 if rvm is not initialized", trans == -1);

	char * segments[5];

	segments[0] = (char *) rvm_map(rvm, "segment1", 10000);
	st = seqsrchst_get(&rvm->segst, "segment1");
	st->cur_trans = segments[0];
	trans = rvm_begin_trans(rvm, 1, (void**)segments);
	assertTrue(" 7b", "Transaction should return -1 if segment is related to another transaction", trans == -1);
	st->cur_trans = NULL;

	segment2 = malloc(1);
	trans = rvm_begin_trans(rvm, 1, (void**)segment2);
	free(segment2);
	assertTrue(" 7c", "Transaction should return -1 if segment is not mapped", trans == -1);

	segments[0] = (char *) rvm_map(rvm, "segment1", 20);
	segments[1] = (char *) rvm_map(rvm, "segment2", 20);
	trans = rvm_begin_trans(rvm, 2, (void**)segments);
	assertTrue(" 7d", "Transaction should not return -1 if it's successful", trans != -1);	
	assertTrue(" 7e", "Transaction should return reference to rvm", trans->rvm == rvm);	
	assertTrue(" 7f", "Transaction should indicate number of segments related to the transaction", trans->numsegs == 2);	
	assertTrue(" 7g", "Transaction should keep a reference to all the segments", strcmp(trans->segments[0]->segname, "segment1") == 0 && strcmp(trans->segments[1]->segname, "segment2") == 0);	
	assertTrue(" 7h", "Segments should reference the transaction", trans->segments[0]->cur_trans == trans && trans->segments[1]->cur_trans == trans);	

	/*
	 * TEST 8 - rvm_about_to_modify
	 */

	st = seqsrchst_get(&rvm->segst, "segment1");
	rvm_about_to_modify(NULL, segments[0], 0, 1);
	assertTrue(" 8a", "Modification shouldn't be registered if specified transaction is NULL", steque_size(&st->mods) == 0);

	rvm_about_to_modify((trans_t)-1, segments[0], 0, 1);
	assertTrue(" 8b", "Modification shouldn't be registered if specified transaction is -1", steque_size(&st->mods) == 0);

	segment2 = malloc(1);
	rvm_about_to_modify(trans, segment2, 0, 1);
	assertTrue(" 8c", "Nothing should happen if specified segment doesn't exist", 1 == 1);
	free(segment2);

	segments[3] = (char *) rvm_map(rvm, "segment3", 20);
	st = seqsrchst_get(&rvm->segst, "segment3");
	rvm_about_to_modify(trans, segments[3], 0, 1);
	assertTrue(" 8d", "Modification shouldn't be registered if the segment's transaction is NULL", steque_size(&st->mods) == 0);

	st->cur_trans = st;
	rvm_about_to_modify(trans, segments[3], 0, 1);
	assertTrue(" 8e", "Modification shouldn't be registered if segment doesn't belong to the same transaction", steque_size(&st->mods) == 0);

	sprintf(segments[0], "value-1");
	rvm_about_to_modify(trans, segments[0], 0, 3);
	st = seqsrchst_get(&rvm->segst, "segment1");
	assertTrue(" 8f", "Modification should be registered", steque_size(&st->mods) == 1);	

	mod_t * mod = steque_front(&st->mods);
	assertTrue(" 8g", "Modification record should keep track of the current segment value for undo purposes", strcmp(mod->undo, "val") == 0);	
	assertTrue(" 8h", "Modification record should specify size", mod->size == 3);	
	assertTrue(" 8i", "Modification record should specify offset", mod->offset == 0);	

	rvm_about_to_modify(trans, segments[0], 2, 4);
	assertTrue(" 8j", "It is legal to try to modify same memory area multiple times", steque_size(&st->mods) == 2);	

	rvm_about_to_modify(trans, segments[1], 0, 1);
	st = seqsrchst_get(&rvm->segst, "segment2");
	assertTrue(" 8k", "Modification for different segments should also be registered", steque_size(&st->mods) == 1);	

	/*
	 * TEST 9 - rvm_commit
	 */

	remove(log_filename);

	rvm_commit_trans(NULL); 
	assertTrue(" 9a", "Log file shouldn't be created if specified transaction is NULL", stat(log_filename, &file_stat) != 0);

	rvm_commit_trans((trans_t) -1); 
	assertTrue(" 9b", "Log file shouldn't be created if specified transaction is -1", stat(log_filename, &file_stat) != 0);

	rvm_commit_trans(trans); 
	assertTrue(" 9c", "Log file should be created if specified transaction is valid", stat(log_filename, &file_stat) == 0);
	assertTrue(" 9d", "Log file should have all 3 modifications", file_stat.st_size == 86);

	segment_t st1 = seqsrchst_get(&rvm->segst, "segment1");
	segment_t st2 = seqsrchst_get(&rvm->segst, "segment2");

	assertTrue(" 9e", "List of modifications should be removed after commit", steque_size(&st1->mods) == 0 && steque_size(&st2->mods) == 0);
	assertTrue(" 9f", "Segments should not be associated with a transaction after commit", st1->cur_trans == NULL && st2->cur_trans == NULL);

	remove(log_filename);
	trans = rvm_begin_trans(rvm, 1, (void**)segments);
	rvm_about_to_modify(trans, segments[0], 0, 3);

	sprintf(segments[0], "abc");
	rvm_commit_trans(trans);

	int comparator = 1;
	char buffer[100];
	FILE * log_fd = fopen(log_filename, "r");

	fgets(buffer, sizeof(buffer), log_fd);
	comparator = comparator && strcmp(buffer, "TRANSACTION\n") == 0;

	fgets(buffer, sizeof(buffer), log_fd);
	comparator = comparator && strcmp(buffer, "segment1\n") == 0;

	fgets(buffer, sizeof(buffer), log_fd);
	comparator = comparator && strcmp(buffer, "0\n") == 0;

	fgets(buffer, sizeof(buffer), log_fd);
	comparator = comparator && strcmp(buffer, "3\n") == 0;

	fgets(buffer, sizeof(buffer), log_fd);
	comparator = comparator && strcmp(buffer, "abc\n") == 0;

	close(log_fd);

	assertTrue(" 9g", "Content of the log file is accurate", comparator);

	/*
	 * TEST 10 - rvm_abort_trans
	 */

	rvm_abort_trans(NULL); 
	assertTrue("10a", "Nothing should happen if specified transaction is NULL", 1 == 1); 

	rvm_abort_trans((trans_t) - 1); 
	assertTrue("10b", "Nothing should happen if specified transaction is -1", 1 == 1); 

	sprintf(segments[0], "value-1");
	sprintf(segments[1], "value-2");
	trans = rvm_begin_trans(rvm, 2, (void**)segments);
	rvm_about_to_modify(trans, segments[0], 0, 3);
	rvm_about_to_modify(trans, segments[0], 6, 1);
	rvm_about_to_modify(trans, segments[1], 6, 1);

	sprintf(segments[0], "abcue-1");
	sprintf(segments[0] + 6, "x");
	sprintf(segments[1] + 6, "y");

	rvm_abort_trans(trans);

	st1 = seqsrchst_get(&rvm->segst, "segment1");
	st2 = seqsrchst_get(&rvm->segst, "segment2");
	assertTrue("10c", "Memory should be restored when transaction is aborted", strcmp(segments[0], "value-1") == 0 && strcmp(segments[1], "value-2") == 0); 
	assertTrue("10d", "List of modifications should be removed after abort", steque_size(&st1->mods) == 0 && steque_size(&st2->mods) == 0);
	assertTrue("10e", "Segments should not be associated with a transaction after abort", st1->cur_trans == NULL && st2->cur_trans == NULL);


	/* Deallocating resources and removing files*/
	free(segment_file_name);

	segment_file_name = get_segment_file_name(directory, "segment1");
	remove(segment_file_name);
	
	segment_file_name = get_segment_file_name(directory, "segment2");
	remove(segment_file_name);

	segment_file_name = get_segment_file_name(directory, "segment3");
	remove(segment_file_name);

	free(segment_file_name);
	free(log_filename);

	printf("%d Tests, %d Failed.\n", test_count, failed_test_count);

	return 0;
}

static void copy_test_template(char * source, char * target, char * directory) {
	char source_filename [14 + strlen(source) + 1];
	strcpy(source_filename, "test_templates/");
	strcat(source_filename, source);

	char target_filename [strlen(directory) + strlen(target) + 1];
	strcpy(target_filename, directory);
	strcat(target_filename, "/");
	strcat(target_filename, target);

	struct stat st1;
	if (stat(source_filename, &st1) == 0) {
		FILE * fd = fopen(source_filename, "r");
		void * mem = malloc(st1.st_size);
		fread(mem, 1, st1.st_size, fd);
		fclose(fd);

		fd = fopen(target_filename, "w");
		fwrite(mem, 1, st1.st_size, fd);
		fclose(fd);
	}
	else {
		printf("Error loading template file %s\n", source_filename);
		exit(1);
	}
}

static char * get_segment_file_name(char * directory, char * segname) {
	char * filename = malloc(strlen(directory) + strlen(segname) + 1);
	strcpy(filename, directory);
	strcat(filename, "/");
	strcat(filename, segname);	

	return filename;
}

static char * get_log_file_name(char * directory) {
	char * filename = malloc(strlen(directory) + 8 + 1);
	strcpy(filename, directory);
	strcat(filename, "/rvm.log");

	return filename;
}

static void assertTrue(char * test, char * message, bool condition) {
	test_count++;

    if (condition) {
        fprintf(stderr, "PASSED - [TEST %s] %s\n", test, message);
    }
    else {
    	fprintf(stderr, "FAILED - [TEST %s] %s\n", test, message);	
    	failed_test_count++;
    }
}

