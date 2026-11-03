#ifndef __DOCTREE_HH__
#define __DOCTREE_HH__

#include "lout/misc.hh"

class DoctreeNode {
   public:
      DoctreeNode *parent;
      DoctreeNode *sibling;
      DoctreeNode *lastChild;
      int num; // unique ascending id
      int element;
      lout::misc::SimpleVector<char*> *klass;
      const char *pseudo;
      const char *id;
      bool focused;
      bool focusWithin;
      struct Attribute { char *name, *value; };
      lout::misc::SimpleVector<Attribute> attributes;
      int childIndex;

      DoctreeNode () {
         parent = NULL;
         sibling = NULL;
         lastChild = NULL;
         klass = NULL;
         pseudo = NULL;
         id = NULL;
         focused = false;
         focusWithin = false;
         element = 0;
         num = -1;
         childIndex = 0;
      };

      ~DoctreeNode () {
         for (int i = 0; i < attributes.size(); i++) {
            dFree(attributes.getRef(i)->name);
            dFree(attributes.getRef(i)->value);
         }
         dFree ((void*) id);
         while (lastChild) {
            DoctreeNode *n = lastChild;
            lastChild = lastChild->sibling;
            delete n;
         }
         if (klass) {
            for (int i = 0; i < klass->size (); i++)
               dFree (klass->get(i));
            delete klass;
         }
      }

      void setAttribute(const char *name, const char *value) {
         if (getAttribute(name))
            return; // HTML uses the first occurrence of a duplicate attribute
         attributes.increase();
         Attribute *a = attributes.getLastRef();
         a->name = dStrdup(name);
         a->value = dStrdup(value);
      }

      const char *getAttribute(const char *name) const {
         for (int i = 0; i < attributes.size(); i++)
            if (!dStrAsciiCasecmp(attributes.get(i).name, name))
               return attributes.get(i).value;
         return NULL;
      }
};

/**
 * \brief HTML document tree interface.
 *
 * The Doctree class defines the interface to the parsed HTML document tree
 * as it is used for CSS selector matching.
 */
class Doctree {
   private:
      DoctreeNode *topNode;
      DoctreeNode *rootNode;
      int num;

   public:
      Doctree () {
         rootNode = new DoctreeNode;
         topNode = rootNode;
         num = 0;
      };

      ~Doctree () {
         delete rootNode;
      };

      DoctreeNode *push () {
         DoctreeNode *dn = new DoctreeNode ();
         dn->parent = topNode;
         dn->sibling = dn->parent->lastChild;
         dn->childIndex = dn->sibling ? dn->sibling->childIndex + 1 : 1;
         dn->parent->lastChild = dn;
         dn->num = num++;
         topNode = dn;
         return dn;
      };

      void pop () {
         assert (topNode != rootNode); // never pop the root node
         topNode = topNode->parent;
      };

      inline DoctreeNode *top () {
         if (topNode != rootNode)
            return topNode;
         else
            return NULL;
      };

      inline DoctreeNode *parent (const DoctreeNode *node) {
         if (node->parent != rootNode)
            return node->parent;
         else
            return NULL;
      };

      inline DoctreeNode *sibling (const DoctreeNode *node) {
         return node->sibling;
      };
};

#endif
