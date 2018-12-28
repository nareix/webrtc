package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path"
	"runtime"
	"strings"
	"time"
	"bytes"
	"path/filepath"
)

func Exists(name string) bool {
    if _, err := os.Stat(name); err != nil {
    if os.IsNotExist(err) {
                return false
            }
    }
    return true
}

func Realpath(fpath string) (string, error) {

	if len(fpath) == 0 {
		return "", os.ErrInvalid
	}

	if !filepath.IsAbs(fpath) {
		pwd, err := os.Getwd()
		if err != nil {
			return "", err
		}
		fpath = filepath.Join(pwd, fpath)
	}

	path := []byte(fpath)
	nlinks := 0
	start := 1
	prev := 1
	for start < len(path) {
		c := nextComponent(path, start)
		cur := c[start:]

		switch {

		case len(cur) == 0:
			copy(path[start:], path[start+1:])
			path = path[0 : len(path)-1]

		case len(cur) == 1 && cur[0] == '.':
			if start+2 < len(path) {
				copy(path[start:], path[start+2:])
			}
			path = path[0 : len(path)-2]

		case len(cur) == 2 && cur[0] == '.' && cur[1] == '.':
			copy(path[prev:], path[start+2:])
			path = path[0 : len(path)+prev-(start+2)]
			prev = 1
			start = 1

		default:

			fi, err := os.Lstat(string(c))
			if err != nil {
				return "", err
			}
			if isSymlink(fi) {

				nlinks++
				if nlinks > 16 {
					return "", os.ErrInvalid
				}

				var link string
				link, err = os.Readlink(string(c))
				after := string(path[len(c):])

				// switch symlink component with its real path
				path = switchSymlinkCom(path, start, link, after)

				prev = 1
				start = 1
			} else {
				// Directories
				prev = start
				start = len(c) + 1
			}
		}
	}

	for len(path) > 1 && path[len(path)-1] == os.PathSeparator {
		path = path[0 : len(path)-1]
	}
	return string(path), nil

}

// test if a link is symbolic link
func isSymlink(fi os.FileInfo) bool {
	return fi.Mode()&os.ModeSymlink == os.ModeSymlink
}

// switch a symbolic link component to its real path
func switchSymlinkCom(path []byte, start int, link, after string) []byte {

	if link[0] == os.PathSeparator {
		// Absolute links
		return []byte(filepath.Join(link, after))
	}

	// Relative links
	return []byte(filepath.Join(string(path[0:start]), link, after))
}

// get the next component
func nextComponent(path []byte, start int) []byte {
	v := bytes.IndexByte(path[start:], os.PathSeparator)
	if v < 0 {
		return path
	}
	return path[0 : start+v]
}

var ldlinuxpath = "/lib64/ld-linux-x86-64.so.2"

func otoolL(lib string) (paths []string, err error) {
	c := exec.Command("otool", "-L", lib)
	stdout, _ := c.StdoutPipe()
	br := bufio.NewReader(stdout)
	if err = c.Start(); err != nil {
		err = fmt.Errorf("otoolL: %s", err)
		return
	}
	for i := 0; ; i++ {
		var line string
		var rerr error
		if line, rerr = br.ReadString('\n'); rerr != nil {
			break
		}
		if i == 0 {
			continue
		}
		f := strings.Fields(line)
		if len(f) >= 2 && strings.HasPrefix(f[1], "(") {
			paths = append(paths, f[0])
		}
	}
	return
}

func installNameTool(lib string, change [][]string) error {
	if len(change) == 0 {
		return nil
	}
	args := []string{}
	for _, c := range change {
		args = append(args, "-change")
		args = append(args, c[0])
		args = append(args, c[1])
	}
	args = append(args, lib)

	return runcmd("install_name_tool", args...)
}

type CopyEntry struct {
	Realpath string
	IsBin    bool
}

func packlibDarwin(copy []CopyEntry) error {
	visited := map[string]bool{}
	isbin := map[string]bool{}

	var dfs func(k string) error
	dfs = func(k string) error {
		if visited[k] {
			return nil
		}
		visited[k] = true
		paths, err := otoolL(k)
		if err != nil {
			return err
		}
		for _, p := range paths {
			if strings.HasPrefix(p, "@") {
				const rpath = "@rpath/"
				if strings.HasPrefix(p, rpath) {
					lp := locate(strings.TrimPrefix(p, rpath))
					if lp != "" {
						p = lp
					} else {
						continue
					}
				} else {
					continue
				}
			}
			if strings.HasPrefix(p, "/usr/lib") {
				continue
			}
			if strings.HasPrefix(p, "/System") {
				continue
			}
			if err := dfs(p); err != nil {
				return err
			}
		}
		return nil
	}

	for _, f := range copy {
		if f.IsBin {
			isbin[f.Realpath] = true
		}
		if err := dfs(f.Realpath); err != nil {
			return err
		}
	}

	change := [][]string{}
	for p := range visited {
		if !strings.HasPrefix(p, "/") {
			continue
		}
		fname := path.Join("lib", path.Base(p))
		change = append(change, []string{p, fname})
	}

	for p := range visited {
		dstdir := "lib"
		if isbin[p] {
			dstdir = "bin"
		}
		fname := path.Join(dstdir, path.Base(p))
		if err := runcmd("cp", "-f", p, fname); err != nil {
			return err
		}
		if err := runcmd("chmod", "744", fname); err != nil {
			return err
		}
		if err := installNameTool(fname, change); err != nil {
			return err
		}
		fmt.Println("copy", fname)
	}

	return nil
}

var libsearchpath []string

func locate(name string) (out string) {
	for _, root := range libsearchpath {
		p := path.Join(root, name)
		_, serr := os.Stat(p)
		if serr == nil {
			out = p
			return
		}
	}
	return
}

type Entry struct {
	Name     string
	Realpath string
	IsBin    bool
}

func ldd(lib string) (paths []Entry, err error) {
	c := exec.Command("ldd", lib)
	stdout, _ := c.StdoutPipe()
	if err = c.Start(); err != nil {
		err = fmt.Errorf("ldd: %s", err)
		return
	}
	br := bufio.NewReader(stdout)
	for {
		line, rerr := br.ReadString('\n')
		if rerr != nil {
			break
		}
		f := strings.Fields(line)
		if len(f) < 3 {
			continue
		}
		if strings.HasSuffix(f[0], ":") {
			continue
		}
		name := f[0]
		if name == "" {
			continue
		}
		if name == "linux-vdso.so.1" {
			continue
		}
		if name == ldlinuxpath {
			continue
		}
		var realpath string
		if strings.HasPrefix(f[2], "/") {
			realpath = f[2]
		} else {
			realpath = locate(name)
		}
		paths = append(paths, Entry{Name: name, Realpath: realpath})
	}
	return
}

func packlibLinux(copy []CopyEntry) error {
	visited := map[string]Entry{}

	var dfs func(e Entry) error
	dfs = func(e Entry) (err error) {
		if _, ok := visited[e.Name]; ok {
			return
		}
		visited[e.Name] = e
		var paths []Entry
		if paths, err = ldd(e.Realpath); err != nil {
			return
		}
		for _, p := range paths {
			if err = dfs(p); err != nil {
				return
			}
		}
		return
	}

	for _, f := range copy {
		dfs(Entry{Name: path.Base(f.Realpath), Realpath: f.Realpath, IsBin: f.IsBin})
	}

	cp := func(src, dst string) error {
		if err := runcmd("cp", "-f", src, dst); err != nil {
			return err
		}
		fmt.Println("copy", src, dst)
		return nil
	}

	finddbglib := func(src string) (file string) {
		if f := path.Join("/usr/lib/debug", src); Exists(f) {
			return f
		}
		return
	}

	for _, e := range visited {
		if e.Realpath == "" {
			continue
		}

		src, err := Realpath(e.Realpath)
		if err != nil {
			return err
		}

		dstdir := "lib"
		if e.IsBin {
			dstdir = "bin"
		}
		dst := path.Join(dstdir, e.Name)
		if err := cp(src, dst); err != nil {
			return err
		}

		if dbglib := finddbglib(src); dbglib != "" {
			if err := cp(dbglib, path.Join("libdbg", e.Name)); err != nil {
				return err
			}
		}
	}

	if err := cp(ldlinuxpath, path.Join("lib", "ld-linux.so")); err != nil {
		return err
	}

	return nil
}

func runPack(copy []CopyEntry) error {
	for _, d := range []string{"lib", "bin", "libdbg"} {
		os.RemoveAll(d)
		os.Mkdir(d, 0744)
	}
	switch runtime.GOOS {
	case "darwin":
		if err := packlibDarwin(copy); err != nil {
			return err
		}
	case "linux":
		if err := packlibLinux(copy); err != nil {
			return err
		}
	}
	return nil
}

func runUpload(name string) error {
	uploadname := fmt.Sprintf("%s-%s.tar.bz2", name, runtime.GOOS)
	tarname := fmt.Sprintf("/tmp/%d", time.Now().UnixNano())
	defer os.Remove(tarname)

	if err := runcmd("tar", "cjf", tarname, "bin", "lib"); err != nil {
		return err
	}

	if err := runcmd("qup", tarname, uploadname); err != nil {
		return err
	}

	return nil
}

func runcmd(path string, args ...string) error {
	c := exec.Command(path, args...)
	c.Stdout = os.Stdout
	c.Stderr = os.Stderr
	if err := c.Run(); err != nil {
		err = fmt.Errorf("run %v %v failed", path, args)
		return err
	}
	return nil
}

func run() error {
	upload := flag.Bool("u", false, "upload")
	search := flag.String("s", "", "lib search path")
	bin := flag.String("bin", "", "bin files")
	lib := flag.String("lib", "", "lib files")
	name := flag.String("n", "", "name")
	flag.Parse()

	if *name == "" {
		return fmt.Errorf("name is empty")
	}

	if *search != "" {
		libsearchpath = strings.Split(*search, ";")
	}

	copy := []CopyEntry{}
	if *bin != "" {
		for _, f := range strings.Split(*bin, ";") {
			copy = append(copy, CopyEntry{IsBin: true, Realpath: f})
		}
	}
	if *lib != "" {
		for _, f := range strings.Split(*lib, ";") {
			copy = append(copy, CopyEntry{Realpath: f})
		}
	}

	if len(copy) == 0 {
		return fmt.Errorf("specifiy some lib or bin files")
	}

	if err := runPack(copy); err != nil {
		return err
	}

	if *upload {
		if err := runUpload(*name); err != nil {
			return err
		}
	}

	return nil
}

func main() {
	if err := run(); err != nil {
		fmt.Println(err)
	}
}
