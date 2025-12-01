export TAINT_OPTIONS="output_dir=out:taint_file=stdin:debug=1"
./build/bin/fgtest ./dummy out/seed.bin ctwm_index.json traces.json rewards.json