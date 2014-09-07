matrix: library
	cd matrix;gcc -m32 -w -g -o matrix gt_matrix.c ../gt_kthread.c ../gt_pq.c ../gt_signal.c ../gt_spinlock.c ../gt_uthread.c
newmatrix: library
	cd matrix;gcc -m32 -w -g -o newmatrix new_matrix.c ../gt_kthread.c ../gt_pq.c ../gt_signal.c ../gt_spinlock.c ../gt_uthread.c
library:
	gcc -m32 -w -g -c gt_kthread.c gt_pq.c gt_signal.c gt_spinlock.c gt_uthread.c; ar rcs libuthread.a *.o       
clean:
	rm *.o *.a matrix/matrix









