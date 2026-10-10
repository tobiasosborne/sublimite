/* Actual panel request -> exact count and first 4096 offsets, fresh mapping.
 * The caller runs this inside find_fixture_supervise, including all cleanup. */
typedef struct count_arguments {
    const char *path;
    size_t samples;
    bool track, word_pair;
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
    unsigned variants=args->word_pair ? 2u : 1u;
    uint64_t *values=calloc(args->samples * variants,sizeof *values);
    if (!values) { (void)close(fd); return 2; }
    bench_samples times[2];
    for (unsigned variant=0;variant<variants;variant++)
        bench_samples_init(&times[variant],values + variant * args->samples,args->samples);
    /* Qualified repeated requests use warm 64KiB mappings. Each sample
     * completes a real asynchronous search and checks its ranges, including
     * the plain/whole-word pair; this is not the full-1GB throughput gate. */
    length=65536u;
    for (size_t sample=0;sample<args->samples;sample++) {
      for (unsigned variant=0;variant<(args->word_pair ? 2u : 1u);variant++) {
        edit_arena arena;
        if (edit_arena_init(&arena,16u*1024u*1024u)) { (void)close(fd); return 2; }
        piece_allocator allocator={&arena,allocate,release};
        piece_tree *tree=piece_create(&allocator);
        work_pool *pool=edit_arena_alloc(&arena,sizeof *pool,_Alignof(work_pool));
        findui_panel panel={0};
        if (!tree || !pool || work_pool_init(pool,1,0)) _exit(2);
        /* Prefix offsets are independent of the initial/late/empty viewport. */
        findui_config config={&arena,pool,FIND_MAX_OFFSETS,1,NULL,NULL};
        const uint64_t windows[][2]={{0,1},{length-1,length},{0,0}};
        uint64_t window_start=windows[sample%3][0], window_end=windows[sample%3][1];
        if (findui_init(&panel,&config)!=FINDUI_OK) _exit(2);
        uint64_t start=bench_now_ns(), deadline=start+UINT64_C(30000000000);
        void *mapping=args->word_pair ? mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)
                                     : mmap(NULL,length,PROT_READ,MAP_PRIVATE,fd,0);
        if (mapping==MAP_FAILED) _exit(2);
        if (args->word_pair) {
            uint8_t *bytes=mapping;
            for (size_t i=0;i<length;i++) bytes[i]=i%2 ? (uint8_t)' ' : (uint8_t)'a';
            start=bench_now_ns(); deadline=start+UINT64_C(30000000000);
        }
        if (piece_init_mapped(tree,mapping,length,NULL)) _exit(2);
        piece_snapshot *snapshot=piece_snapshot_take(tree);
        if (!snapshot) _exit(2);
        findui_code code=findui_set_source(&panel,snapshot,1);
        piece_snapshot_release(snapshot);
        if (code!=FINDUI_OK ||
            findui_set_window(&panel,window_start,window_end)!=FINDUI_OK ||
            findui_show(&panel,true,false)!=FINDUI_OK ||
            findui_set_options(&panel,(findui_options){false,true,args->word_pair && variant==1})!=FINDUI_OK ||
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
        (void)bench_add(&times[variant],bench_now_ns()-start);
        if (!state.complete || state.match_count!=(args->word_pair ? length/2 : length) || state.cached_matches!=FIND_MAX_OFFSETS ||
            state.selected.start!=0 || state.selected.end!=1) _exit(2);
        for (size_t i=1;i<FIND_MAX_OFFSETS;i++) {
            findui_range range;
            if (findui_next(&panel,1,&range)!=FINDUI_OK || range.start!=(args->word_pair ? 2*i : i) || range.end!=(args->word_pair ? 2*i+1 : i+1)) _exit(2);
        }
        findui_range last; size_t shown=0;
        uint64_t expected=(window_start<window_end && (!args->word_pair || window_start%2==0)) ? 1u : 0u;
        if (findui_highlights(&panel,window_start,window_end,&last,1,&shown)!=FINDUI_OK ||
            shown!=expected || (shown && (last.start!=window_start || last.end!=window_start+1))) _exit(2);
        while ((code=findui_dispose(&panel))==FINDUI_MORE) {
            if (bench_now_ns()>=deadline) _exit(2);
            (void)work_mailbox_drain(pool,route,&panel); pause_worker();
        }
        if (code!=FINDUI_OK) _exit(2);
        work_pool_shutdown(pool); piece_destroy(tree); edit_arena_free(&arena);
        (void)munmap(mapping,length);
      }
    }
    (void)close(fd);
    char power[64]; bench_battery_status(power,sizeof power);
    int rc=0;
    for (unsigned variant=0;variant<(args->word_pair ? 2u : 1u);variant++) {
        int verdict=bench_gate_report(args->word_pair ? (variant ? "G6_findui_word_a_space_64KiB" : "G6_findui_plain_a_space_64KiB")
                                                     : "G6_findui_dense_a_64KiB",
                                      &times[variant],UINT64_C(80000000),UINT64_C(125000000),
                                      BENCH_INTERACTION_MIN_N,args->track,power,"-");
        if (verdict) rc=verdict;
    }
    printf("findui_count: exact 64KiB total/first 4096 checked; initial/late/empty windows; mapping=NEW; (M)%s\n",bench__tag_from_power(power));
    free(values);
    return rc;
}
