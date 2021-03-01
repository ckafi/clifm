# CliFM
> The KISS file manager: CLI-based, ultra-lightweight, lightning fast, and written in C

###
[![clifm](https://img.shields.io/aur/version/clifm?color=1793d1&label=clifm&logo=arch-linux&style=for-the-badge)](https://aur.archlinux.org/packages/clifm/)
[![clifm-git](https://img.shields.io/aur/version/clifm-git?color=1793d1&label=clifm-git&logo=arch-linux&style=for-the-badge)](https://aur.archlinux.org/packages/clifm/)
[![clifm-colors-git](https://img.shields.io/aur/version/clifm-colors-git?color=1793d1&label=clifm-colors-git&logo=arch-linux&style=for-the-badge)](https://aur.archlinux.org/packages/clifm/)
[![privacy](https://img.shields.io/badge/privacy-ok-green?style=for-the-badge)](https://en.wikipedia.org/wiki/Privacy-invasive_software)
[![License](https://img.shields.io/github/license/leo-arch/clifm?color=red&style=for-the-badge)](https://github.com/leo-arch/clifm/blob/master/LICENSE)

## Rationale

Why in this world do we need another file manager? In the first place, just because I can do it, write it, and learn (a lot) in the process, just because this is a free world, and very specially, a free community; and, needless to say, alternatives are at the heart of freedom.

Secondly, because I'm sure I'm not the only person in this world looking for a non-bloated, KISS file manager (on Arch's notion of simplcity see: https://wiki.archlinux.org/index.php/Arch_Linux#Simplicity): it just does whatever needs to be done using as little resources as possible. No GUI, no curses, but just a command line, shell-like file manager: 5MiB of RAM and 400KiB of disk space (plus some willingness to try something different and new) is all you need.

Finally, because CliFM, unlike most file managers out there, is certainly a file manager, but also **a shell extension**. Almost everything you do on your shell can be done in this file manager as well: search for files, copy, rename, and trash some of them, but, at the same time, update/upgrade your system, add some cronjob, stop a service, and run nano (or vi, or emacs, if you like).

## Description

![clifm](images/clifm.png)

CliFM is a completely command-line-based, shell-like file manager able to perform all the basic operations you may expect from any other FM. Besides these common operations, such as copy, move, remove, etc, CLiFM most distinctive features are:

* It is REALLY **CLI-based**. No GUI (like Thunar and the like) nor TUI or curses (like Ranger or nnn) at all, just a command line. Since it does not need any GUI, it can run on the Linux built-in console and even on a SSH or any other remote session.

* With a memory footprint below 5 MiB and a disk usage of 0.4 MiB, it is incredibly **lightweight and fast**, and as such, able to run on really old hardware. 

* The use of **short (and even one-character) commands**, and list numbers (**ELN's**) for filenames. For example, instead of typing: `cp file1 file2 file3 file4 dir/`, you can do this: `c 1-4 7`. Shorter and quicker. If the auto-cd and auto-open functions are enabled, which is the default, you can change to directories or open files by just entering the corresponding ELN. So, instead of `cd 12` or `o 12` you can just enter `12`; even shorter and quicker. As a plus, ELN's can also be used with external commands. Example: `diff 1 5` or `ls -l 12 14`. If numbers are a bit tricky to you, as they are to me, you can use the TAB key to expand the ELN to the corresponding filename. So, type `r 56`, then TAB, and it becomes `r filename`.

* **Bookmarks**: With CLiFM bookmarks function, accessing your preferred files and/or directories be as easy as this: `bm` (or `Alt-b`), to call the bookmarks function, and then `1` (or whatever is the number corresponding to your bookmark).

![bookmarks](images/bookmarks.png)

* **Files selection**: the ability to select (and deselect) files from here and there, even in different instances of the program, and then operate on them as you whish via the Selection Box or the `sel` keyword. Example: `s 1 4 56 33` will send the files corresponding to these ELN's to the Selection Box. Then, by typing `sb` you can check the contents of the Selection Box. Let's suppose you want to copy a couple of files from your home directory to some distant path, say `/media/data/misc`. Instead of copying all these files individually, you just select the files and then tell the `paste` command where to copy them:
 
`s 1 2 3 6` (or `s 1-3 6`) and then `paste sel /media/data/misc`

You can also use the 'sel' keyword with external commands. Example: `s 1-4 7 10 && file sel`.

Of course, you can deselect some or all selected files whenever you want with the `desel` or `ds` command: `ds *`, or just press `Alt-d`.

![selection box](images/sel_box.png)
 
 * Open files without the need to specify any program. Via `mime` (the **built-in resource opener**), if no program was specified, CliFM will open the file with the default program associated to that kind of files. To open a file may be as simple as this: `o 12`, or `o 12 &` if you want it running in the background. Of course, you can also set a custom resource opener.

* **Quick search**: type `/REGEX` and CliFM will list all matches for the corresponding REGEX pattern. Example: `/.*.png`
 will list all the PNG files in the current directory. If you want to 
search for files in another directory, just tell the search function 
where to search: `/.*.png /media/images`. And, if you want to further filter the search, you still can specify what kind of files you want. For example: `/[.-].*d$ -d /etc` will list all directories (-d) in /etc containing a dot or a slash and ending with 'd'.

![quick search](images/quick_search.png)

* A Freedesktop compliant **trash system** to be able to recover deleted files.

* **Extended color codes**: Just like the `ls` command, CLiFM uses (customizable) color codes to identify file types and extensions. However, and unlike `ls`, CLiFM is also able to distinguish between empty and non-empty files or directories, broken and non-broken symlinks, files and directories with or without read permission, multi-hardlink files, and more. Once in CliFM, type `colors` or `cc` to see the list of currently used color codes.

![colors](images/colors.png)

* **Files counter**: It also displays the amount of files contained by listed directories or symlinks to directories.

![dirs](images/dirs.png)

* **Directory history map**: Keep in sight previous, current, and next entries in the directory history list for easiest and fastest navigation through visited directories.

* **NEW: PLUGINS via custom actions**: Use custom action names, as if they were any other command, to run custom shell scripts and extend thus CliFM functionality to fit your needs. This is basically an easy way of building custom commands for CliFM.

* **NEW: Stealth mode:** Leave no trace on the host system.

* **NEW:** Quickly and easily navigate through the jump database (a list of visited directories and the amount of times each of them was visited) via the **autojump** function.

![autojump](images/autojump0.png)
![autojump](images/autojump2.png)

* **NEW: Icons support** :smirk: (depends on the `icons-in-terminal` project. See https://github.com/sebastiencs/icons-in-terminal)

1) [install](https://github.com/sebastiencs/icons-in-terminal#installation) icons-in-terminal.
2) Run CliFM with the `--icons` command line option, or, once in the program, enter `icons on`.

![icons](images/icons.png)

Because file manager, but also half-shell, CLiFM also provides the following features:

* TAB-completion for commands, paths, ELN's, profiles, bookmarks, color schemes, and the autojump function
* Bash-like quoting system
* History function
* Shell commands execution 
* Aliases
* Logs
* Prompt and profile commands
* Bash-like prompt customization
* Sequential and conditional commands execution 
* User profiles
* Customizable keyboard shortcuts (**NEW**)
* Lira, a built-in resource opener
* Mas, a built-in pager for files listing
* Multiple sorting methods: name, size, atime, btime, ctime, mtime, version, extension, and inode. It also supports reverse sorting.
* Bulk rename
* Archives and compression support (including Zstandard and ISO 9660)
* Auto-cd and auto-open
* Symlinks editor
* Disk usage (**NEW**)
* CD on quit, file picker (as shell functions) (**NEW**)
* PDF reader, image/video previews, wallpaper setter, music playlist (plugins) (**NEW**)
* Color schemes (**NEW**)

**NOTE:** By default, CliFM ships only one color scheme, but more can be found at https://github.com/leo-arch/clifm-colors. The package is also available in the AUR: https://aur.archlinux.org/packages/clifm-colors-git

Finally, all CLiFM options could be handled directly via command line, by passing parameters to the program, or via plain
text configuration files, located in `$XDG_CONFIG_HOME/clifm/`.

Insofar as it is heavily inspired by Arch Linux and its KISS principle, CLiFM is fundamentally aimed to be lightweight, fast, and simple. And if you think it's not fast enough, you can always try the **light mode** to make it even faster.

## Dependencies:

`glibc` and `coreutils`, of course, but also `libcap`, `acl`, `file`, and `readline`. For archers: All these dependenciess are part of the `core` reposiroty. In Debian systems two packages must be installed before compilation: `libcap-dev` and `libreadline-dev`. In Fedora based systems you need `libcap-devel` and `readline-devel`. Optional dependencies: `sshfs`, `curlftpfs`, and `cifs-utils` (for remote filesystems support), and `atool`, `archivemount`, `genisoimage`, `p7zip`, and `cdrtools` (for archiving and compression support).

## Compiling and Running CliFM:

### Arch Linux

You'll find the corresponding packages on the AUR: https://aur.archlinux.org/packages/clifm and, for the development version, https://aur.archlinux.org/packages/clifm-git. 

Of course, you can also clone, build, and install the package using the PKGBUILD file:

	$ git clone https://github.com/leo-arch/clifm.git
	$ cd clifm
	$ makepkg -si

### Other Linux distributions (or FreeBSD):

1. Clone the repository

        $ git clone https://github.com/leo-arch/clifm.git
        $ cd clifm

2. You have two options here:

#### Via make

Run `make` (*this is the recommended procedure*) as follows:

	$ make build && sudo make install

You should find the binary file in `/usr/bin`, so that you can run it as any other program:

	$ clifm

To uninstall `clifm` issue this command wherever the Makefile is located:

	$ sudo make uninstall

#### Manually via `gcc` (`tcc` and `clang` also work). 

##### On Linux:

	$ gcc -O3 -march=native -fstack-protector-strong -s -o clifm clifm.c -lcap -lreadline -lacl

To enable POSIX compliance, pass this option to the compiler: `-D_BE_POSIX.` The only two features disabled in this way are: a) files birth time, only available on Linux via **statx(2)**, which is Linux-specific, and **strverscmp(3)**, a GNU extension used to sort files by version.

##### On FreeBSD:

	$ gcc -O3 -march=native -fstack-protector-strong -s -o clifm clifm.c -lintl -lreadline

Run the binary file produced by `gcc`:

	$ ./clifm

Of course, you can copy this binary to `/usr/bin` or `/usr/local/bin`, or anywhere in your PATH, and then run the program as always:

	$ clifm

Do not forget to install the manpage as well (the full help is in here):

	$ sudo cp manpage /usr/share/man/man1/clifm.1
	$ sudo gzip /usr/share/man/man1/clifm.1

Then you can access the manpage as always: `man clifm`

## Support

ClifM is C99 and POSIX-1.2008 compliant (if compiled with the `_BE_POSIX` flag). It works on Linux and FreeBSD, on i686, x86_64, and ARM architectures.

## First steps

Try `help` command to learn more about CliFM. Once in the CliFM prompt, type `help` or `?`:

	12:12 user:hostname /etc
	:) $ help

To jumtp into the COMMANDS section in the manpage, simply enter `cmd`.

Just try it and let me know. It gets better and better. I myself use it as my main, and indeed only, file manager; it couldn't be so bad, isn't it?
