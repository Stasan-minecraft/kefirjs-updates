#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    UpView v={.screen=UP_READY,.compatible=true};
    assert(up_input(&v,UP_PRIMARY)==UP_CHECK);
    for (int r=0;r<2;r++) {
        v.screen=r?UP_INSTALLING:UP_CHECKING;
        assert(up_input(&v,UP_BACK)==UP_NONE && up_input(&v,UP_PRIMARY)==UP_NONE);
    }
    up_result(&v,KF_AVAILABLE);
    strcpy(v.release.current,"1.1.0");
    strcpy(v.release.latest,"1.2.0");
    strcpy(v.release.notes,"One.\nTwo.\nThree.\nFour.\nFive.\nSix.\nSeven.\nEight.\nNine.");
    assert(up_pages(&v)==2);
    assert(up_input(&v,UP_NEXT)==UP_NONE && v.page==1);
    assert(up_input(&v,UP_NEXT)==UP_NONE && v.page==1);
    assert(up_input(&v,UP_PREVIOUS)==UP_NONE && !v.page);
    assert(up_input(&v,UP_PRIMARY)==UP_NONE && v.screen==UP_CONFIRM);
    assert(up_input(&v,UP_BACK)==UP_NONE && v.screen==UP_AVAILABLE);
    assert(up_input(&v,UP_PRIMARY)==UP_NONE && v.screen==UP_CONFIRM);
    assert(up_input(&v,UP_PRIMARY)==UP_APPLY);
    v.compatible=false;
    assert(up_input(&v,UP_PRIMARY)==UP_NONE);
    v.screen=UP_AVAILABLE;
    assert(up_input(&v,UP_PRIMARY)==UP_NONE && v.screen==UP_AVAILABLE);
    up_result(&v,KF_APPLIED);
    assert(up_input(&v,UP_PRIMARY)==UP_CLOSE);
    up_result(&v,KF_RECOVERED);
    assert(up_input(&v,UP_PRIMARY)==UP_CHECK);
    up_result(&v,KF_ERROR);
    assert(v.screen==UP_ERROR && up_input(&v,UP_PRIMARY)==UP_CHECK);
    up_result(&v,KF_CURRENT);
    assert(up_input(&v,UP_BACK)==UP_CLOSE);
    assert(up_touch(1055,648)==UP_PRIMARY && up_touch(115,635)==UP_BACK);
    assert(up_touch(1202,551)==UP_NEXT && up_touch(993,552)==UP_PREVIOUS);
    assert(up_touch(514,185)==-1);
    puts("Updater navigation, review/cancel/commit and paging passed.");
    return 0;
}
