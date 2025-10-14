#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include "gpio_driver.h"
#include <linux/kthread.h>
#include <linux/delay.h>

// Etiqueta para el autor del modulo
#define AUTHOR	"TP5_Multithreading"

// Puntero para primer hilo
static struct task_struct *thread1;
// Puntero para segundo hilo
static struct task_struct *thread2;


static int thread1_f(void *params) {
    while(!kthread_should_stop()) {
        printk(KERN_INFO "%s: Hola desde el Kernel :D!!\n", AUTHOR);
        msleep(1000);
    }
    return 0;
}

static int thread2_f(void *params) {
    while(!kthread_should_stop()) {
        printk(KERN_INFO "%s: Chau desde el Kernel :D!!\n", AUTHOR);
        msleep(2000);
    }
    return 0;
}


// Operaciones de archivos
static struct file_operations chrdev_ops = {
	.owner = THIS_MODULE
};
dev_t chrdev_number;
// Variable que representa el char device
struct cdev chrdev;
// Clase del char device
struct class *chrdev_class;

/**
 * @brief Se llama cuando el modulo se carga en el kernel
*/
static int __init kernel_module_init(void) {
	// TODO
	thread1 = kthread_run(
        thread1_f,  // Callback
        NULL,       // Sin datos
        "thread1"   // Nombre del hilo
    );
    if (IS_ERR(thread1)) {
        printk(KERN_ERR "%s: Error al crear thread 1\n", AUTHOR);
        return -1;
    }
    thread2 = kthread_run(
        thread2_f,  // Callback
        NULL,       // Sin datos
        "thread2"   // Nombre del hilo
    );
    if (IS_ERR(thread2)) {
        printk(KERN_ERR "%s: Error al crear thread 2\n", AUTHOR);
        // Elimino el hilo anterior
        kthread_stop(thread1);
        return -1;
    }
	printk(KERN_INFO"%s: Se inicializo tp5 ejemplo multithreading\n", AUTHOR);
	return 0;
}

/**
 * @brief Se llama cuando el modulo se quita del kernel
 */
static void __exit kernel_module_exit(void) {
	pr_info("%s: Removiendo el modulo de kernel\n", AUTHOR);
    // Si se habia podido crear el hilo
	if (thread1) {
        // Detengo el hilo
        kthread_stop(thread1);
    }
    // Si se habia podido crear el hilo
    if (thread2) {
        // Detengo el hilo
        kthread_stop(thread2);
    }
	printk("%s: Modulo removido\n",AUTHOR);	
}

// Registro la funcion de inicializacion y salida
module_init(kernel_module_init);
module_exit(kernel_module_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("UTN FRA Tecnicas Digitales III - TP5: GPOS");
