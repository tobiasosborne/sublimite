/* Actual panel request -> exact count and first 4096 offsets, fresh mapping.
 * The caller runs this inside find_fixture_supervise, including all cleanup. */
typedef struct count_arguments {
    const char *path;
    size_t samples;
    bool gate;
} count_arguments;
static int count_worker(void *argument)
{
    count_arguments *args=argument;
    int fd=open(args->path,O_RDONLY);
    struct stat metadata;
    if (fd<0 || fstat(fd,&metadata) || metadata.st_size!=(off_t)UINT64_C(1073741824)) {
        if (fd>=0) (void)close(fd);
        fprintf(stderr,"findui_count: missing/wrong fixture %s\n",args->path);
        return 2;
    }
    size_t length=(size_t)metadata.st_size;
    uint8_t block[65536]; size_t warmed=0;
    while (warmed<length) {
        ssize_t got=read(fd,block,sizeof block);
        if (got<0 && errno==EINTR) continue;
        if (got<=0) { (void)close(fd); return 2; }
        for (size_t i=0;i<(size_t)got;i++) if (block[i]!='a') { (void)close(fd); return 2; }
        warmed+=(size_t)got;
    }
    uint64_t values[64]; bench_samples times; bench_samples_init(&times,values,64);
    for (size_t sample=0;sample<args->samples;sample++) {
        edit_arena arena;
        if (edit_arena_init(&arena,16u*1024u*1024u)) { (void)close(fd); return 2; }
        piece_allocator allocator={&arena,allocate,release};
        piece_tree *tree=piece_create(&allocator);
        work_pool *pool=edit_arena_alloc(&arena,sizeof *pool,_Alignof(work_pool));
        findui_panel panel={0};
        if (!tree || !pool || work_pool_init(pool,1,0)) _exit(2);
        /* The panel's shared range budget reserves at least one visible slot.
         * Publish offsets 0..4094 in its prefix and offset 4095 in that slot. */
        findui_config config={&arena,pool,FIND_MAX_OFFSETS-1,1,NULL,NULL};
        if (findui_init(&panel,&config)!=FINDUI_OK) _exit(2);
        uint64_t start=bench_now_ns(), deadline=start+UINT64_C(30000000000);
        void *mapping=mmap(NULL,length,PROT_READ,MAP_PRIVATE,fd,0);
        if (mapping==MAP_FAILED || piece_init_mapped(tree,mapping,length,NULL)) _exit(2);
        piece_snapshot *snapshot=piece_snapshot_take(tree);
        if (!snapshot) _exit(2);
        findui_code code=findui_set_source(&panel,snapshot,1);
        piece_snapshot_release(snapshot);
        if (code!=FINDUI_OK ||
            findui_set_window(&panel,FIND_MAX_OFFSETS-1,FIND_MAX_OFFSETS)!=FINDUI_OK ||
            findui_show(&panel,true,false)!=FINDUI_OK ||
            findui_set_options(&panel,(findui_options){false,true,false})!=FINDUI_OK ||
            findui_set_query(&panel,(const uint8_t *)"a",1)!=FINDUI_OK) _exit(2);
        findui_state state;
        do {
            if (bench_now_ns()>=deadline || findui_service(&panel)>FINDUI_MORE) {
                fputs("findui_count: search completion deadline/error\n",stderr); _exit(2);
            }
            (void)work_mailbox_drain(pool,route,&panel);
            state=findui_get_state(&panel);
            if (state.searching) pause_worker();
        } while (state.searching);
        (void)bench_add(&times,bench_now_ns()-start);
        if (!state.complete || state.match_count!=length || state.cached_matches!=FIND_MAX_OFFSETS-1 ||
            state.selected.start!=0 || state.selected.end!=1) _exit(2);
        for (size_t i=1;i<FIND_MAX_OFFSETS-1;i++) {
            findui_range range;
            if (findui_next(&panel,1,&range)!=FINDUI_OK || range.start!=i || range.end!=i+1) _exit(2);
        }
        findui_range last; size_t shown=0;
        if (findui_highlights(&panel,FIND_MAX_OFFSETS-1,FIND_MAX_OFFSETS,&last,1,&shown)!=FINDUI_OK ||
            shown!=1 || last.start!=FIND_MAX_OFFSETS-1 || last.end!=FIND_MAX_OFFSETS) _exit(2);
        while ((code=findui_dispose(&panel))==FINDUI_MORE) {
            if (bench_now_ns()>=deadline) _exit(2);
            (void)work_mailbox_drain(pool,route,&panel); pause_worker();
        }
        if (code!=FINDUI_OK) _exit(2);
        work_pool_shutdown(pool); piece_destroy(tree); edit_arena_free(&arena);
        (void)munmap(mapping,length);
    }
    (void)close(fd);
    int miss=bench_report("G6_findui_dense_a",&times,UINT64_C(80000000),UINT64_C(125000000));
    printf("findui_count: exact total/first 4096 PASS; mapping=NEW; (M)%s; verdict=%s\n",
           bench_evidence_tag(),args->gate ? (miss ? "MISS" : "PASS") : "TRACK");
    return args->gate && miss ? 1 : 0;
}
