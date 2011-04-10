@echo off
rd /q /s package
md package
copy despotify-bin\bin\* package\
md package\user-components\foo_input_spotify
copy foobar2000\user-components\foo_input_spotify\foo_input_spotify.dll package\user-components\foo_input_spotify
copy foobar2000\user-components\foo_input_spotify\foo_input_spotify.pdb package\user-components\foo_input_spotify

cd package
sha1sum user-components/foo_input_spotify/foo_input_spotify.dll
sha1sum *.dll
cd ..
