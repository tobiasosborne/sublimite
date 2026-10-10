Fixed §1: reload validates complete descriptor and canonical-entry identities.  
Red reproduced torn A/B bytes; green preserves the old edited tree.  
`make all`: 0; savectl and file sanitizer tests: 0.  
`make check`: 2, blocked by `editor_close_test:42`, also reproduced with original HEAD sources.  
[Worker report](/home/tobias/Projects/sublimite/.wt/edit-mdv/docs/worker-reports/edit-mdv-s8.md); §§9–20 untouched.